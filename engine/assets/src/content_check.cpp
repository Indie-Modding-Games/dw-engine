#include <dwe/assets/content_check.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/media_headers.hpp>
#include <dwe/assets/model.hpp>
#include <dwe/assets/sound.hpp>
#include <dwe/assets/texture.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <optional>
#include <thread>
#include <utility>

namespace dwe::assets {

namespace {

using Opener = core::Result<void> (*)(std::span<const std::byte>);

// The most threads the check uses. Assets are independent, so they are opened in parallel.
constexpr unsigned MaxThreads = 16;

// Turns the result of a reader into success or its error, dropping what it read.
template <auto Reader>
core::Result<void> discardValue(std::span<const std::byte> bytes) {
    if (auto read = Reader(bytes); !read) {
        return std::unexpected{read.error()};
    }
    return {};
}

// Transcodes a cooked texture to BC7, as the engine loads it on every GPU that samples BC7.
core::Result<void> openKtx2(std::span<const std::byte> bytes) {
    if (auto texture = decodeKtx2(bytes, TexelEncoding::Bc7); !texture) {
        return std::unexpected{texture.error()};
    }
    return {};
}

core::Result<void> openYaml(std::span<const std::byte> bytes) {
    const std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    if (auto document = core::parseYaml(text); !document) {
        return std::unexpected{document.error()};
    }
    return {};
}

constexpr std::array<std::pair<std::string_view, Opener>, 11> Openers = {{
    {"png", discardValue<decodePng>},
    {"ktx2", openKtx2},
    {"glb", discardValue<readModel>},
    {"gltf", discardValue<readModel>},
    {"yaml", openYaml},
    {"wav", discardValue<readWavHeader>},
    {"opus", discardValue<decodeOpus>},
    {"ogg", discardValue<decodeOpus>},
    {"sf2", discardValue<readSoundFontHeader>},
    {"mid", discardValue<readMidiHeader>},
    {"y4m", discardValue<readY4mHeader>},
}};

} // namespace

core::Result<void> openAsset(std::string_view extension, std::span<const std::byte> bytes) {
    for (const auto& [known, opener] : Openers) {
        if (known == extension) {
            return opener(bytes);
        }
    }
    return core::fail(std::format("the engine has no reader for .{} files", extension));
}

std::vector<ContentProblem> checkContent(const VirtualFileSystem& fileSystem) {
    const std::vector<AssetId> ids = fileSystem.ids();
    std::vector<std::optional<ContentProblem>> found(ids.size());
    std::atomic<std::size_t> next{0};
    const auto worker = [&] {
        for (std::size_t i = next++; i < ids.size(); i = next++) {
            const AssetSource* source = fileSystem.resolve(ids[i]);
            auto bytes = fileSystem.read(ids[i]);
            if (!bytes) {
                found[i] =
                    ContentProblem{.id = ids[i], .file = pathText(source->file), .message = bytes.error().message};
                continue;
            }
            const std::string extension = source->file.extension().string();
            const auto opened = openAsset(std::string_view{extension}.substr(extension.empty() ? 0 : 1), *bytes);
            if (!opened) {
                found[i] =
                    ContentProblem{.id = ids[i], .file = pathText(source->file), .message = opened.error().message};
            }
        }
    };
    const unsigned count = std::clamp(std::thread::hardware_concurrency(), 1U, MaxThreads);
    std::vector<std::thread> threads;
    for (unsigned i = 1; i < count; ++i) {
        threads.emplace_back(worker);
    }
    worker();
    for (auto& thread : threads) {
        thread.join();
    }
    std::vector<ContentProblem> problems;
    for (auto& problem : found) {
        if (problem) {
            problems.push_back(std::move(*problem));
        }
    }
    return problems;
}

} // namespace dwe::assets
