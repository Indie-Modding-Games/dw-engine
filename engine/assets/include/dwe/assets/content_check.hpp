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

// Opens the bytes of an asset with the engine's reader for its file type, by extension: png
// decodes, ktx2 (a cooked texture) transcodes to BC7, glb loads as the game loads a model
// (parseModel, with its skin, weights and clip extras), gltf parses and validates, yaml parses,
// opus and ogg (Ogg Opus, as dwx cook writes sounds) decode, wav, sf2, mid and y4m read their
// headers. An extension the engine cannot read is an error.
core::Result<void> openAsset(std::string_view extension, std::span<const std::byte> bytes);

// Opens an asset as openAsset does and, when the game reads a document of that id with a reader of
// its own, with that reader too: map/<id>/data with parseMapDocument and the game tables
// (data/maps, data/digimon...) with theirs, which refuse unknown and missing keys, and the text
// tables (localization/*/* and text/*/*) with parseTextTable.
core::Result<void> openAsset(const AssetId& id, std::string_view extension,
                             std::span<const std::byte> bytes);

// An asset the engine could not read or open.
struct ContentProblem {
  AssetId id;
  std::string file;
  std::string message;
};

// Reads every asset of the stack as the game reads it (the file that wins, or a content table
// composed entry by entry from every pack that has it: each kind's table and the ones the rulesets
// name, table_kinds.hpp) and opens it: an image the game reads from its PNG (pngReadingOf) with
// that reader, any other asset with openAsset by its id, a content table with the reader of its
// kind; and checks that the Digimon the packs add have their model contracts. Several assets at a
// time, so the file systems of the packs are read from several threads. Returns the problems, by
// id.
std::vector<ContentProblem> checkContent(const VirtualFileSystem& fileSystem);

} // namespace dwe::assets
