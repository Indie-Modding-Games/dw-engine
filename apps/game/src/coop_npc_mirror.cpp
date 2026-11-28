#include "coop_npc_mirror.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <set>

#include "coop_world.hpp"

namespace dwe::app {

namespace coop = game::coop;

std::size_t CoopNpcMirror::show(game::field::FieldNpcs& npcs, const View& view, net::Millis now) {
  if (view.world == nullptr || view.battling) {
    npcs.releaseMirrors();
    missingSince_.clear();
    return 0;
  }
  if (serial_ != view.mapSerial) {
    serial_ = view.mapSerial;
    missingSince_.clear();
  }
  std::set<std::size_t> listed;
  for (const PresentedCoopEntity& entity : view.world->entities) {
    if (entity.role != coop::EntityRole::Npc || entity.owner != coop::HostPlayerId ||
        entity.networkId < coop::NpcNetworkBase) {
      continue;
    }
    const std::size_t slot = entity.networkId - coop::NpcNetworkBase;
    listed.insert(slot);
    (void)npcs.mirror({.slot = slot,
                       .position = entity.position,
                       .yaw = entity.yaw,
                       .motion = sceneMotionOf(entity.motion)});
  }
  // A snapshot with no NPC, or full, may leave some out: it tells nothing of those missing.
  const bool whole = !listed.empty() && view.world->entities.size() < coop::MaxSnapshotEntities;
  std::size_t removed = 0;
  std::set<std::size_t> missing;
  for (const game::field::NpcPose& pose : npcs.poses()) {
    if (listed.contains(pose.slot)) {
      continue;
    }
    (void)npcs.releaseMirror(pose.slot);
    if (!whole || !view.ready) {
      continue;
    }
    missing.insert(pose.slot);
    const auto [since, added] = missingSince_.try_emplace(pose.slot, now);
    if (now - since->second >= GoneAfter && npcs.unloadAt(pose.slot)) {
      core::logInfo("coop: the host's map has no NPC in slot {} any more: taken off", pose.slot);
      missing.erase(pose.slot);
      ++removed;
    }
  }
  std::erase_if(missingSince_, [&](const auto& entry) { return !missing.contains(entry.first); });
  return removed;
}

} // namespace dwe::app
