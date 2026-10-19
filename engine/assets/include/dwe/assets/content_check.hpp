#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

// Opens the bytes of an asset with the engine's reader for its file type, by extension: png decodes, glb and gltf
// parse and validate, yaml parses, wav, sf2, mid and y4m read their headers. An extension the engine cannot read is
// an error.
core::Result<void> openAsset(std::string_view extension, std::span<const std::byte> bytes);

// An asset the engine could not read or open.
struct ContentProblem {
    AssetId id;
    std::string file;
    std::string message;
};

// Reads the file that wins for every asset of the stack and opens it with openAsset. Returns the problems, by id.
std::vector<ContentProblem> checkContent(const VirtualFileSystem& fileSystem);

} // namespace dwe::assets
