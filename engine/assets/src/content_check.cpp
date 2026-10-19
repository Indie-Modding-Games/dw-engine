#include <dwe/assets/content_check.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/media_headers.hpp>
#include <dwe/assets/model.hpp>
#include <dwe/core/yaml.hpp>

#include <array>
#include <format>
#include <utility>

namespace dwe::assets {

namespace {

using Opener = core::Result<void> (*)(std::span<const std::byte>);

// Turns the result of a reader into success or its error, dropping what it read.
template <auto Reader>
core::Result<void> discardValue(std::span<const std::byte> bytes) {
    if (auto read = Reader(bytes); !read) {
        return std::unexpected{read.error()};
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

constexpr std::array<std::pair<std::string_view, Opener>, 8> Openers = {{
    {"png", discardValue<decodePng>},
    {"glb", discardValue<readModel>},
    {"gltf", discardValue<readModel>},
    {"yaml", openYaml},
    {"wav", discardValue<readWavHeader>},
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
    std::vector<ContentProblem> problems;
    for (const AssetId& id : fileSystem.ids()) {
        const AssetSource* source = fileSystem.resolve(id);
        const std::string file = pathText(source->file);
        auto bytes = fileSystem.read(id);
        if (!bytes) {
            problems.push_back({.id = id, .file = file, .message = bytes.error().message});
            continue;
        }
        const std::string extension = source->file.extension().string();
        if (auto opened = openAsset(std::string_view{extension}.substr(extension.empty() ? 0 : 1), *bytes); !opened) {
            problems.push_back({.id = id, .file = file, .message = opened.error().message});
        }
    }
    return problems;
}

} // namespace dwe::assets
