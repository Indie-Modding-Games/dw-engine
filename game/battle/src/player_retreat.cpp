#include <dwe/game/battle/player_retreat.hpp>

#include <optional>
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
  facing_ = facingToward(position_, point);
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
  // The run's root motion (anim.c: applyRootMomentum) keeps the fraction of a unit its steps leave;
  // the step the area test tries is the same (map.c: entityMoveForward).
  const std::int64_t x = (static_cast<std::int64_t>(facing_.x) * timing.retreatStep) + remainder_.x;
  const std::int64_t z = (static_cast<std::int64_t>(facing_.z) * timing.retreatStep) + remainder_.z;
  const Point whole = wholeUnits(x, z);
  const Point ahead = {.x = position_.x + whole.x, .z = position_.z + whole.z};
  if (!stops && ground != nullptr) {
    stops =
        leavesScreenArea(*ground, timing, position_, ahead, timing.retreatRadius, width, height);
  }
  if (stops) {
    stop(partner);
    return true;
  }
  position_ = ahead;
  remainder_ = {
      .x = static_cast<std::int32_t>(x - (static_cast<std::int64_t>(whole.x) * FacingScale)),
      .z = static_cast<std::int32_t>(z - (static_cast<std::int64_t>(whole.z) * FacingScale))};
  return false;
}

void PlayerRetreat::stop(Point partner) {
  running_ = false;
  lookAt(partner);
}

PlayerFlight::PlayerFlight(std::vector<assets::CollisionCell> way, assets::CollisionCell entry,
                           std::optional<Point> away, Point from, Facing facing,
                           const TimingRules& timing)
    : way_(std::move(way)), entry_(entry), away_(away), position_(from), facing_(facing),
      timing_(&timing), wayLeft_(way_.size()) {
  // handleCommands, case 1: a player already on the entry cell turns to the way out at once.
  if (cellOf(position_) == entry_ && away_) {
    facing_ = facingToward(position_, *away_);
  }
}

// handleFleeing: the next cell of the way back (counted reached once on it), else the entry cell,
// and on the entry cell the way out. entityLookAtTile and entityLookAtLocation turn at once.
void PlayerFlight::turn() {
  const assets::CollisionCell cell = cellOf(position_);
  if (wayLeft_ > 0) {
    const assets::CollisionCell next = way_.at(way_.size() - wayLeft_);
    facing_ = facingToward(position_, centreOf(next));
    if (cell == next) {
      --wayLeft_;
    }
    return;
  }
  if (cell != entry_) {
    facing_ = facingToward(position_, centreOf(entry_));
    return;
  }
  // On the entry cell it faces the way out and runs on the spot (animFlag 2: applyRootMomentum
  // moves it no more).
  if (away_) {
    facing_ = facingToward(position_, *away_);
  }
  held_ = true;
}

// tamerTickBattle: tickAnimation carries the run's root motion a step on the way it faces; the
// player is no fighter and tests no collision.
void PlayerFlight::step() {
  if (!held_) {
    turn();
  }
  if (held_) {
    return;
  }
  const std::int64_t x =
      (static_cast<std::int64_t>(facing_.x) * timing_->retreatStep) + remainder_.x;
  const std::int64_t z =
      (static_cast<std::int64_t>(facing_.z) * timing_->retreatStep) + remainder_.z;
  const Point whole = wholeUnits(x, z);
  position_ = {.x = position_.x + whole.x, .z = position_.z + whole.z};
  remainder_ = {
      .x = static_cast<std::int32_t>(x - (static_cast<std::int64_t>(whole.x) * FacingScale)),
      .z = static_cast<std::int32_t>(z - (static_cast<std::int64_t>(whole.z) * FacingScale))};
}

Point wayOut(Point spawn, Facing spawnFacing, std::int32_t distance) {
  return {.x = spawn.x - static_cast<std::int32_t>(static_cast<std::int64_t>(spawnFacing.x) *
                                                   distance / FacingScale),
          .z = spawn.z - static_cast<std::int32_t>(static_cast<std::int64_t>(spawnFacing.z) *
                                                   distance / FacingScale)};
}

} // namespace dwe::game::battle
