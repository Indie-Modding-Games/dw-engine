#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/message.hpp>

#include <system_error>

#include "messages.hpp"

namespace dwe::assets {
namespace {

// The folders that make a folder a checkout of the decompilation: its headers and its decompiled C.
constexpr std::string_view DecompHeadersFolder = "include/dw";
constexpr std::string_view DecompSourcesFolder = "src";

bool hasDecompLayout(const std::filesystem::path& root) {
    std::error_code error;
    return std::filesystem::is_directory(root / DecompHeadersFolder, error) &&
           std::filesystem::is_directory(root / DecompSourcesFolder, error);
}

core::Result<void> failFor(std::string_view message, const DevSource& source) {
    return core::fail(core::formatMessage(message, devSourceKindName(source.kind), pathText(source.root)));
}

core::Result<void> validateDiscImage(const DevSource& source) {
    std::error_code error;
    if (!std::filesystem::exists(source.root, error)) {
        return failFor(messages::DevSourceMissing, source);
    }
    if (!std::filesystem::is_regular_file(source.root, error)) {
        return failFor(messages::DevSourceNotFile, source);
    }
    return {};
}

core::Result<void> validateFolder(const DevSource& source) {
    std::error_code error;
    if (!std::filesystem::exists(source.root, error)) {
        return failFor(messages::DevSourceMissing, source);
    }
    if (!std::filesystem::is_directory(source.root, error)) {
        return failFor(messages::DevSourceNotFolder, source);
    }
    return {};
}

core::Result<void> validateExtractedContent(const DevSource& source) {
    if (auto folder = validateFolder(source); !folder) {
        return folder;
    }
    const DiskFileSystem disk;
    auto packs = findPacks(disk, source.root);
    if (!packs) {
        return core::fail(core::formatMessage(messages::DevSourceUnreadable, devSourceKindName(source.kind),
                                              pathText(source.root), packs.error().message));
    }
    if (packs->packs.empty()) {
        return failFor(messages::DevSourceNoPacks, source);
    }
    return {};
}

core::Result<void> validateDecompRoot(const DevSource& source) {
    if (auto folder = validateFolder(source); !folder) {
        return folder;
    }
    if (!hasDecompLayout(source.root)) {
        return failFor(messages::DevSourceNoDecompLayout, source);
    }
    return {};
}

core::Result<void> validateSource(const DevSource& source) {
    switch (source.kind) {
    case DevSourceKind::DiscImage:
        return validateDiscImage(source);
    case DevSourceKind::ExtractedContent:
        return validateExtractedContent(source);
    case DevSourceKind::DecompRoot:
        return validateDecompRoot(source);
    }
    return {};
}

} // namespace

std::string_view devSourceKindName(DevSourceKind kind) {
    switch (kind) {
    case DevSourceKind::DiscImage:
        return messages::DiscImageKind;
    case DevSourceKind::ExtractedContent:
        return messages::ExtractedContentKind;
    case DevSourceKind::DecompRoot:
        return messages::DecompRootKind;
    }
    return {};
}

bool DevAssetSources::empty() const {
    return !discImage && extractedContent.empty() && !decompRoot;
}

std::vector<DevSource> DevAssetSources::list() const {
    std::vector<DevSource> sources;
    if (discImage) {
        sources.push_back({.kind = DevSourceKind::DiscImage, .root = *discImage});
    }
    for (const std::filesystem::path& folder : extractedContent) {
        sources.push_back({.kind = DevSourceKind::ExtractedContent, .root = folder});
    }
    if (decompRoot) {
        sources.push_back({.kind = DevSourceKind::DecompRoot, .root = *decompRoot});
    }
    return sources;
}

core::Result<void> validateDevAssetSources(const DevAssetSources& sources) {
    for (const DevSource& source : sources.list()) {
        if (auto valid = validateSource(source); !valid) {
            return valid;
        }
    }
    return {};
}

} // namespace dwe::assets
