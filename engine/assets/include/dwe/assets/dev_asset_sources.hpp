#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::assets {

// The kinds of local assets a developer may opt into (docs/architecture/assets-de-desarrollo.md). They are never the
// default for players, and every root is only ever read.
enum class DevSourceKind : std::uint8_t {
    DiscImage,        // the user's disc image (.cue or .bin), which only dwx reads
    ExtractedContent, // a content folder dwx extracted, whose packs are mounted under every normal pack
    DecompRoot,       // a checkout of the decompilation: the reference of behaviour for tools and tests
};

// How logs and errors name a kind ("disc image").
[[nodiscard]] std::string_view devSourceKindName(DevSourceKind kind);

// One root and its kind.
struct DevSource {
    DevSourceKind kind = DevSourceKind::ExtractedContent;
    std::filesystem::path root;

    friend bool operator==(const DevSource&, const DevSource&) = default;
};

// The local assets in use in development. Empty, the default, means none: the normal packs only.
struct DevAssetSources {
    std::optional<std::filesystem::path> discImage;
    // An earlier folder wins over a later one for a pack of the same name.
    std::vector<std::filesystem::path> extractedContent;
    std::optional<std::filesystem::path> decompRoot;

    [[nodiscard]] bool empty() const;
    // Every root with its kind: the disc image, the extracted content folders in order, then the decomp root.
    [[nodiscard]] std::vector<DevSource> list() const;
};

// Checks that each root is there and of its kind: the disc image is a file, an extracted content folder has packs
// (findPacks) and the decomp root has the decompilation's include/dw and src folders. The error names the root and its
// kind.
core::Result<void> validateDevAssetSources(const DevAssetSources& sources);

} // namespace dwe::assets
