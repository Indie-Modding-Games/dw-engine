#include <dwe/game/battle/player_retreat.hpp>

#include <utility>

#include "battle_context.hpp"

namespace dwe::game::battle {

namespace {

assets::CollisionCell cellOf(Point point) {
  return assets::collisionCellAt({static_cast<float>(point.x), 0.0F, static_cast<float>(point.z)});
}

Point centreOf(assets::CollisionCell cell) {
  const glm::vec3 centre = assets::collisionCellCenter(cell);
  return {.x = static_cast<std::int32_t>(centre.x), .z = static_cast<std::int32_t>(centre.z)};
}

} // namespace

PlayerRetreat::PlayerRetreat(std::vector<assets::CollisionCell> way, assets::CollisionCell entry,
                             Point from, Facing facing, const TimingRules& timing)
    : way_(std::move(way)), entry_(entry), position_(from), facing_(facing), timing_(&timing),
      wayLeft_(way_.size()) {}

// map.c: entityLookAtTile and entityLookAtLocation turn at once.
void PlayerRetreat::lookAt(Point point) {
  const std::int64_t dx = static_cast<std::int64_t>(point.x) - position_.x;
  const std::int64_t dz = static_cast<std::int64_t>(point.z) - position_.z;
  const std::int64_t length = integerSqrt((dx * dx) + (dz * dz));
  if (length == 0) {
    return;
  }
  facing_ = {.x = static_cast<std::int32_t>(dx * FacingScale / length),
             .z = static_cast<std::int32_t>(dz * FacingScale / length)};
}

// BTL_handleBattleIntro's loop: the player looks at the next cell of its way (and counts it reached
// once on it) and tests its next step against the larger area; past its way it looks at the entry
// cell and stops on it, or tests the smaller area. A step out of the area stops it facing the
// partner; otherwise BTL_battleTickFrame carries it a step on (its run's root motion; the map and
// the fighters do not stop it).
bool PlayerRetreat::step(const BattleGround* ground, Point partner) {
  if (!running_) {
    return false;
  }
  const assets::CollisionCell cell = cellOf(position_);
  const TimingRules& timing = *timing_;
  bool stops = false;
  std::int32_t width = 0;
  std::int32_t height = 0;
  if (wayLeft_ > 0) {
    const assets::CollisionCell next = way_.at(way_.size() - wayLeft_);
    lookAt(centreOf(next));
    if (cell == next) {
      --wayLeft_;
    }
    width = timing.retreatTrailAreaWidth;
    height = timing.retreatTrailAreaHeight;
  } else {
    lookAt(centreOf(entry_));
    stops = cell == entry_;
    width = timing.retreatEntryAreaWidth;
    height = timing.retreatEntryAreaHeight;
  }
  const Point ahead = {
      .x = position_.x + static_cast<std::int32_t>(static_cast<std::int64_t>(facing_.x) *
                                                   timing.retreatStep / FacingScale),
      .z = position_.z + static_cast<std::int32_t>(static_cast<std::int64_t>(facing_.z) *
                                                   timing.retreatStep / FacingScale)};
  if (!stops && ground != nullptr) {
    stops =
        leavesScreenArea(*ground, timing, position_, ahead, timing.retreatRadius, width, height);
  }
  if (stops) {
    stop(partner);
    return true;
  }
  position_ = ahead;
  return false;
}

void PlayerRetreat::stop(Point partner) {
  running_ = false;
  lookAt(partner);
}

} // namespace dwe::game::battle
