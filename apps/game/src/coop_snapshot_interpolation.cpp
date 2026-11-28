#include "coop_snapshot_interpolation.hpp"

#include <algorithm>
#include <cmath>

namespace dwe::app {

bool CoopSnapshotInterpolation::push(game::coop::Snapshot snapshot, net::Millis now) {
  const net::Millis sentAt = SnapshotPeriod * static_cast<std::int64_t>(snapshot.worldTick);
  const auto poseOf = [&](const game::coop::EntityState& state) {
    return game::coop::PlayerPose{.mapId = snapshot.mapId,
                                  .x = state.x,
                                  .y = state.y,
                                  .z = state.z,
                                  .yaw = state.yaw,
                                  .motion = state.motion};
  };
  // The host's world tick counts on across maps: an older snapshot is late, whatever its map.
  if (latest_ && !game::coop::sequenceNewer(snapshot.worldTick, latest_->worldTick)) {
    // One of this map still fills the tracks it reached in time (PoseTrack drops it otherwise).
    if (snapshot.mapId == latest_->mapId) {
      for (const game::coop::EntityState& state : snapshot.entities) {
        if (const auto track = tracks_.find(state.networkId); track != tracks_.end()) {
          (void)track->second.push(snapshot.worldTick, sentAt, poseOf(state), now);
        }
      }
    }
    return false;
  }
  if (latest_ && snapshot.mapId != latest_->mapId)
    tracks_.clear();
  for (const game::coop::EntityState& state : snapshot.entities) {
    (void)tracks_[state.networkId].push(snapshot.worldTick, sentAt, poseOf(state), now);
  }
  // An entity the host no longer sends (a player who left) goes with its track.
  std::erase_if(tracks_, [&](const auto& track) {
    return std::ranges::find(snapshot.entities, track.first, &game::coop::EntityState::networkId) ==
           snapshot.entities.end();
  });
  latest_ = std::move(snapshot);
  return true;
}

std::optional<PresentedCoopWorld> CoopSnapshotInterpolation::sample(net::Millis now) const {
  if (!latest_)
    return std::nullopt;
  PresentedCoopWorld world{.mapId = latest_->mapId,
                           .worldTick = latest_->worldTick,
                           .acknowledgedCommand = latest_->acknowledgedCommand,
                           .entities = {},
                           .npcsWhole = latest_->npcsWhole};
  world.entities.reserve(latest_->entities.size());
  for (const game::coop::EntityState& state : latest_->entities) {
    const auto track = tracks_.find(state.networkId);
    const auto pose = track != tracks_.end() ? track->second.sample(now) : std::nullopt;
    if (!pose)
      continue;
    world.entities.push_back({.networkId = state.networkId,
                              .owner = state.owner,
                              .role = state.role,
                              .position = {pose->x, pose->y, pose->z},
                              .yaw = pose->yaw,
                              .motion = pose->motion,
                              .action = state.action});
  }
  return world;
}

std::vector<game::coop::PlayerId>
CoopSnapshotInterpolation::playersOn(std::string_view mapId) const {
  std::vector<game::coop::PlayerId> present;
  if (!latest_ || latest_->mapId != mapId)
    return present;
  for (const game::coop::EntityState& state : latest_->entities) {
    if (state.role == game::coop::EntityRole::Player &&
        !std::ranges::contains(present, state.owner))
      present.push_back(state.owner);
  }
  std::ranges::sort(present);
  return present;
}

void CoopSnapshotInterpolation::reset() {
  latest_.reset();
  tracks_.clear();
}

} // namespace dwe::app
