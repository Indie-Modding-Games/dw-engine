#include <dwe/assets/map_warps.hpp>
#include <dwe/assets/messages.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dwe::assets {

namespace {

// Whether some cell of the collision is the exit's trigger, so the player can take the warp.
bool hasTrigger(const MapCollision& collision, std::int32_t exit) {
  const std::int32_t trigger = FirstWarpTrigger + exit;
  return std::ranges::any_of(
      collision.values(), [trigger](std::uint8_t value) { return std::cmp_equal(value, trigger); });
}

std::optional<WarpIssue> resolve(MapWarp& warp, const MapCollision& collision, const MapTable& maps,
                                 bool basePack) {
  const std::optional<std::string> named = warp.targetMapId;
  const bool indexed = warp.targetMap >= 0;
  const MapEntry* byId = named ? maps.find(*named) : nullptr;
  const MapEntry* byIndex = indexed ? maps.findByOriginalIndex(warp.targetMap) : nullptr;
  warp.targetMapId.reset();
  const auto issue = [&](WarpProblem problem, std::string target) {
    return WarpIssue{.exit = warp.exit, .problem = problem, .target = std::move(target)};
  };
  if (named && byId != nullptr && byIndex != nullptr && byId != byIndex) {
    return issue(WarpProblem::Disagree,
                 messages::formatMessage(messages::WarpDisagreeTargets, *named, warp.targetMap,
                                         byIndex->id));
  }
  if (basePack) {
    // The base pack writes both, as the extractor does, and they agree: an id with no index, an
    // index of a map with no id, an index outside the table next to an id or an id of no map is a
    // mistake there.
    if ((named && !indexed) || (!named && byIndex != nullptr)) {
      return issue(WarpProblem::BaseNeedsBoth, named ? *named : std::to_string(warp.targetMap));
    }
    if (named && byId != nullptr && byIndex == nullptr) {
      return issue(WarpProblem::Disagree,
                   messages::formatMessage(messages::WarpDisagreeNoMap, *named, warp.targetMap));
    }
    if (named && byId == nullptr) {
      return issue(WarpProblem::UnknownTarget, *named);
    }
    if (byId != nullptr) {
      warp.targetMapId = byId->id;
    }
    // No id and a raw index outside the table, as the original has: it leads nowhere there either.
    return std::nullopt;
  }
  if (byId != nullptr) {
    warp.targetMapId = byId->id;
    return std::nullopt;
  }
  if (byIndex != nullptr) {
    warp.targetMapId = byIndex->id;
    if (named) {
      return issue(WarpProblem::UnknownIdByIndex, *named);
    }
    return std::nullopt;
  }
  if (named) {
    return issue(WarpProblem::UnknownTarget, *named);
  }
  // No id and no map of the original: -1, or the number the engine gave a pack's map, which a pack
  // never writes.
  if (hasTrigger(collision, warp.exit)) {
    return issue(WarpProblem::Dead, std::to_string(warp.targetMap));
  }
  return std::nullopt; // an unused exit
}

} // namespace

std::vector<WarpIssue> resolveWarpTargets(MapDocument& map, const MapTable& maps, bool basePack) {
  std::vector<WarpIssue> issues;
  for (MapWarp& warp : map.warps) {
    if (auto issue = resolve(warp, map.collision, maps, basePack)) {
      issues.push_back(std::move(*issue));
    }
  }
  return issues;
}

bool disablesWarp(WarpProblem problem) {
  return problem != WarpProblem::UnknownIdByIndex;
}

std::string warpIssueMessage(std::string_view mapId, const WarpIssue& issue) {
  switch (issue.problem) {
  case WarpProblem::Dead:
    return messages::formatMessage(messages::WarpDead, mapId, issue.exit, issue.target);
  case WarpProblem::Disagree:
    return messages::formatMessage(messages::WarpDisagree, mapId, issue.exit, issue.target);
  case WarpProblem::UnknownTarget:
    return messages::formatMessage(messages::WarpUnknownTarget, mapId, issue.exit, issue.target);
  case WarpProblem::BaseNeedsBoth:
    return messages::formatMessage(messages::WarpBaseNeedsBoth, mapId, issue.exit, issue.target);
  case WarpProblem::UnknownIdByIndex:
    return messages::formatMessage(messages::WarpUnknownIdByIndex, mapId, issue.exit, issue.target);
  }
  return {};
}

bool isBaseMapData(const VirtualFileSystem& vfs, std::string_view mapId) {
  const auto id = mapDataId(mapId);
  const AssetSource* source = id ? vfs.resolve(*id) : nullptr;
  if (source == nullptr) {
    return false;
  }
  const auto pack = std::ranges::find(
      vfs.packs(), source->packId, [](const MountedPack& mounted) { return mounted.manifest.id; });
  return pack != vfs.packs().end() && hasBaseRights(pack->origin, pack->manifest.layer);
}

} // namespace dwe::assets
