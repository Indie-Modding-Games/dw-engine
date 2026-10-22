#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/core/collision_triggers.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Where the warps of a map lead (docs/formats/mapas.md#destino-de-una-salida). A warp names its target twice: by the
// original's raw map index (`target_map`) and by the id of data/maps (`target_map_id`). The base pack writes both and
// they agree; a pack's map may name a target by id alone, and an editor that only knows the original's numbers may
// leave the id out. So a known id wins, and otherwise a raw index of 0 or more names the original's map of that index.
// The base pack is the engine's own pack of the base layer (its origin and its layer, as hasBaseRights), never a mod
// that declares that layer.
namespace dwe::assets {

// The first collision trigger of the warps: exit i is trigger 110 + i (tamer.c: checkMapInteraction).
inline constexpr std::int32_t FirstWarpTrigger = core::collision::FirstWarpTrigger;

enum class WarpProblem : std::uint8_t {
    // A pack's map: the collision has the exit's trigger, but the warp names no target: no id, and a target_map that
    // is -1 or no map of the original (such as the number the engine gave a pack's map). An editor may have dropped
    // the target_map_id of a warp into a pack's map.
    Dead,
    // target_map_id names a map and target_map another one of the original; in the base pack, also a target_map that
    // is no map of the original.
    Disagree,
    // target_map_id names no map of data/maps, and target_map none of the original (in the base pack, whatever it is).
    UnknownTarget,
    // The base pack names a target by one of the two only.
    BaseNeedsBoth,
    // A pack's map: target_map_id names no map of data/maps, but target_map one of the original, where the warp
    // leads. The only problem that leaves the warp working.
    UnknownIdByIndex,
};

// Whether the warp of a problem is disabled: every one but UnknownIdByIndex.
[[nodiscard]] bool disablesWarp(WarpProblem problem);

struct WarpIssue {
    std::int32_t exit = 0;
    WarpProblem problem = WarpProblem::Dead;
    // The target as the warp names it, for the message: the id, or the raw index as text.
    std::string target;
};

// Sets each warp's targetMapId to the map it leads to under the rule above, or to nothing when it leads nowhere (an
// unused exit, or one with a problem that disables it). `basePack` says whether the map's data comes from the base
// pack (isBaseMapData), which must name each target both ways. Returns the problems, by exit.
std::vector<WarpIssue> resolveWarpTargets(MapDocument& map, const MapTable& maps, bool basePack);

// The message of a problem, naming the map and the exit; for a dead warp, with the hint that an editor may have
// dropped target_map_id.
std::string warpIssueMessage(std::string_view mapId, const WarpIssue& issue);

// Whether the data of a map comes from the base pack: a pack of the engine's origin and of the base layer.
bool isBaseMapData(const VirtualFileSystem& vfs, std::string_view mapId);

} // namespace dwe::assets
