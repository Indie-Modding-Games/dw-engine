#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace dwe::dwx {

// How the texels of a texture are stored: sRGB-encoded colour, or other data (depth, masks, normals) kept linear.
enum class ColorSpace : std::uint8_t { Srgb, Linear };

// How one asset cooks, decided from its bytes and its sidecar before it is cooked. Part of the cache key.
struct CookOptions {
    // Copied as is, keeping its type, instead of going through the cooker.
    bool passThrough = false;
    ColorSpace colorSpace = ColorSpace::Srgb;
};

// Turns one kind of source file into the form the game loads. A new kind is one more entry in the registry
// (cookers()) and the functions it names. Cookers keep no state: the same bytes and options always cook to the same
// bytes.
struct Cooker {
    std::string_view name;
    // Raised whenever the same source would cook to different bytes, so the cache of earlier cooks is not used.
    std::uint32_t version = 1;
    std::string_view sourceExtension; // "png"
    std::string_view outputExtension; // "ktx2"
    // Decides the options from the asset's id, its source and its sidecar, the YAML at the asset's id plus _meta (null
    // if there is none). Fails if the sidecar says something the cooker does not take. Null: the default options.
    core::Result<CookOptions> (*plan)(const assets::AssetId& id, std::span<const std::byte> source,
                                      const core::YamlNode* sidecar) = nullptr;
    core::Result<std::vector<std::byte>> (*cook)(std::span<const std::byte> source,
                                                 const CookOptions& options) = nullptr;
};

// The cookers dwx knows: PNG to KTX2, GLB checked and kept, and WAV to Ogg Opus. A file of any other type is copied
// as is, and so is every file of a movie (movie/), whose cooked format is not decided.
std::span<const Cooker> cookers();

// Returns the cooker for a source file's extension, or nullptr if the file is copied as is.
const Cooker* findCooker(std::span<const Cooker> registry, std::string_view extension);

// What a cook did, by asset.
struct CookSummary {
    std::size_t assets = 0;
    std::size_t cooked = 0;  // run through a cooker
    std::size_t copied = 0;  // no cooker takes its type
    std::size_t passed = 0;  // a cooker takes its type, but its bytes or its sidecar say to copy it as is
    std::size_t kept = 0;    // left as an earlier cook wrote them: same source, same cooker
    std::size_t shared = 0;  // the same source and cooker as another asset of this cook, so copied from it
    std::size_t removed = 0; // files of an earlier cook that no asset makes any more
    std::uint64_t sourceBytes = 0;
    std::uint64_t outputBytes = 0;
};

// Cooks the pack in one folder into another: the same pack.yaml and asset ids, each file through the cooker of its
// extension unless the cooker's plan passes it through. The pack's dependencies need not be there. The output folder is
// created, or reused if an earlier cook wrote it: the cache in it (.dwx-cook) keys each output by the SHA-1 of its
// source and the cooker's name, version and options, so unchanged files are not cooked again, and files no asset makes
// any more are removed. A folder that dwx cook did not write, or one inside the pack or holding it, is refused. The
// result does not depend on the cache.
core::Result<CookSummary> cookPack(std::span<const Cooker> registry, const std::filesystem::path& pack,
                                   const std::filesystem::path& out);

} // namespace dwe::dwx
