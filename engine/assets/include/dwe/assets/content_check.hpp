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

// Opens the bytes of an asset with the engine's reader for its file type, by extension: png decodes, ktx2 (a cooked
// texture) transcodes to BC7, glb loads as the game loads a model (parseModel, with its skin, weights and clip
// extras), gltf parses and validates, yaml parses, opus and ogg (Ogg Opus, as dwx cook writes sounds) decode, wav,
// sf2, mid and y4m read their headers. An extension the engine cannot read is an error.
core::Result<void> openAsset(std::string_view extension, std::span<const std::byte> bytes);

// Opens an asset as openAsset does and, when the game reads a document of that id with a reader of its own, with that
// reader too: map/<id>/data with parseMapDocument and data/maps with parseMapTable, which refuse unknown and missing
// keys.
core::Result<void> openAsset(const AssetId& id, std::string_view extension, std::span<const std::byte> bytes);

// An asset the engine could not read or open.
struct ContentProblem {
    AssetId id;
    std::string file;
    std::string message;
};

// Reads the file that wins for every asset of the stack and opens it with openAsset by its id, several assets at a
// time, so the file systems of the packs are read from several threads. Returns the problems, by id.
std::vector<ContentProblem> checkContent(const VirtualFileSystem& fileSystem);

} // namespace dwe::assets
