#include "coop_snapshot_interpolation.hpp"

#include <algorithm>
#include <cmath>

namespace dwe::app {

namespace {
bool isNewer(std::uint32_t candidate, std::uint32_t current) {
  const std::uint32_t delta = candidate - current;
  return delta != 0 && delta < 0x80000000u;
}

float interpolateYaw(std::uint16_t from, std::uint16_t to, float alpha) {
  std::int32_t delta = static_cast<std::int32_t>(to) - static_cast<std::int32_t>(from);
  if (delta > 32767)
    delta -= 65536;
  if (delta < -32768)
    delta += 65536;
  float units = static_cast<float>(from) + (static_cast<float>(delta) * alpha);
  if (units < 0.0F)
    units += 65536.0F;
  if (units >= 65536.0F)
    units -= 65536.0F;
  return units * (6.2831853071795864769F / 65536.0F);
}

PresentedCoopEntity present(const game::coop::EntityState& state) {
  return {.networkId = state.networkId,
          .owner = state.owner,
          .role = state.role,
          .position = {static_cast<float>(state.x), static_cast<float>(state.y),
                       static_cast<float>(state.z)},
          .yaw = static_cast<float>(state.yaw) * (6.2831853071795864769F / 65536.0F),
          .motion = state.motion};
}
} // namespace

bool CoopSnapshotInterpolation::push(game::coop::Snapshot snapshot) {
  if (!latest_ || snapshot.mapId != latest_->mapId) {
    previous_.reset();
    latest_ = std::move(snapshot);
    return true;
  }
  if (!isNewer(snapshot.worldTick, latest_->worldTick))
    return false;
  previous_ = std::move(latest_);
  latest_ = std::move(snapshot);
  return true;
}

std::optional<PresentedCoopWorld> CoopSnapshotInterpolation::sample(float alpha) const {
  if (!latest_)
    return std::nullopt;
  const float t = std::isfinite(alpha) ? std::clamp(alpha, 0.0F, 1.0F) : 1.0F;
  PresentedCoopWorld world{.mapId = latest_->mapId,
                           .worldTick = latest_->worldTick,
                           .acknowledgedCommand = latest_->acknowledgedCommand,
                           .entities = {}};
  world.entities.reserve(latest_->entities.size());
  for (const game::coop::EntityState& state : latest_->entities) {
    PresentedCoopEntity entity = present(state);
    if (previous_) {
      const auto prior = std::ranges::find(previous_->entities, state.networkId,
                                           &game::coop::EntityState::networkId);
      if (prior != previous_->entities.end() && prior->owner == state.owner &&
          prior->role == state.role) {
        entity.position =
            glm::mix(glm::vec3{static_cast<float>(prior->x), static_cast<float>(prior->y),
                               static_cast<float>(prior->z)},
                     entity.position, t);
        entity.yaw = interpolateYaw(prior->yaw, state.yaw, t);
      }
    }
    world.entities.push_back(entity);
  }
  return world;
}

void CoopSnapshotInterpolation::reset() {
  previous_.reset();
  latest_.reset();
}

} // namespace dwe::app
