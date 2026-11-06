#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/error.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Partial overrides of a map's data (docs/formats/mapas.md#parches,
// content/schemas/map_patch.schema.json). A pack ships `map/<id>/patch` next to the map's
// `map/<id>/data` to change one exit, one NPC slot, an object's sprite or instance, a light or the
// camera field by field, and the collision whole or by rectangles of cells, without copying the
// whole map. The patches of the pack whose data wins and of every pack above it apply in stack
// order; a field a patch gives replaces the data's whole (a vector, a list of waypoints).
namespace dwe::assets {

class VirtualFileSystem;
struct AssetSource;

// The last segment of a map's patch id.
inline constexpr std::string_view MapPatchLeaf = "patch";

// A map's patch as patchMapDocument applies it.
struct MapPatchText {
  std::string pack;
  // hasBaseRights of the pack: the exits it changes are checked as the base's (MapExitPatch).
  bool baseRights = false;
  // The file, for the errors.
  std::string file;
  std::string text;
};

// The asset id of a map's patch, `map/<id>/patch`; a namespaced id is a folder per part,
// `map/<pack>/<name>/patch`, as mapDataId.
core::Result<AssetId> mapPatchId(std::string_view mapId);

// The map of a `map/<id>/data` or `map/<id>/patch` id, `<pack>:<name>` for
// `map/<pack>/<name>/...`, or nothing for any other id.
std::optional<std::string> mapIdOfAsset(const AssetId& id);

// Whether an id is a map's patch.
bool isMapPatchId(const AssetId& id);

// Checks a patch on its own: its sections, what each change names (an index, an exit, a rectangle
// inside the grid) and a whole collision grid. The values of the fields it replaces are checked
// once applied, by the reader of the data.
core::Result<void> checkMapPatch(std::string_view yaml);

// Parses a map's data and applies the patches to it, bottom to top. An index or an exit that the
// data does not have, or a value that the data's reader refuses once applied, is an error that
// names the patch's file. The result's `patches` lists the packs, and its `patchedExits` the exits
// they changed.
core::Result<MapDocument> patchMapDocument(std::string_view data,
                                           std::span<const MapPatchText> patches);

// The files of the patches that apply to a map: map/<id>/patch of the pack whose data wins and of
// every pack above it, bottom to top. A patch below that pack was written for data that no longer
// wins, so it is left out. None when the map has no data.
std::vector<AssetSource> mapPatchSources(const VirtualFileSystem& vfs, std::string_view mapId);

// Reads the patches of mapPatchSources.
core::Result<std::vector<MapPatchText>> readMapPatches(const VirtualFileSystem& vfs,
                                                       std::string_view mapId);

} // namespace dwe::assets
