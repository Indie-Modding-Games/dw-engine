#include <dwe/core/original_math.hpp>
#include <dwe/game/field/partner_follow.hpp>
#include <dwe/game/field/player_trail.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace dwe::game::field {

namespace {

using assets::CollisionCell;
using assets::RootStep;

// anim.c: the location carries 15 bits of fraction (locX = location << 15).
constexpr std::int32_t FractionBits = 15;
// ApplyMatrixLV on a step << 15 by a matrix of 4096 for 1: the step times the matrix entry, << 3.
constexpr std::int32_t MatrixToFraction = 3;
constexpr std::int32_t AngleMask = 0xFFF;
constexpr std::int32_t QuarterTurn = 0x400;
constexpr std::int32_t HalfTurn = 0x800;
constexpr std::int32_t ThreeQuarters = 0xC00;
constexpr std::int32_t Turn = 0x1000;
constexpr std::int32_t CellSize = 100;
constexpr std::int32_t GridOrigin = 50;

// anim.c: animFlag bit 1 (no step) and the bits collisionGrace sets for each blocked side.
constexpr std::uint8_t HeldStep = 2;
constexpr std::uint8_t NoStepTowardMinusZ = 8;
constexpr std::uint8_t NoStepTowardMinusX = 0x10;
constexpr std::uint8_t NoStepTowardPlusZ = 0x20;
constexpr std::uint8_t NoStepTowardPlusX = 0x40;

// map.c: COLLISION_GRACE_ROTATION: the cardinal rotations collisionGrace tries, by the eighth of a
// turn the partner faces.
constexpr std::array<std::array<std::int32_t, 4>, 8> GraceRotations{{{0x000, 0x400, 0xC00, 0x800},
                                                                     {0x400, 0x000, 0x800, 0xC00},
                                                                     {0x400, 0x800, 0x000, 0xC00},
                                                                     {0x800, 0x400, 0xC00, 0x000},
                                                                     {0x800, 0xC00, 0x400, 0x000},
                                                                     {0xC00, 0x800, 0x000, 0x400},
                                                                     {0xC00, 0x000, 0x800, 0x400},
                                                                     {0x000, 0xC00, 0x400, 0x800}}};
// collisionGrace: the largest turn a frame toward a free side, and the nudge with none free.
constexpr std::int32_t GraceTurn = 0x50;
constexpr std::int32_t StuckTurn = 32;
// map.c: isFiveTileWidePathBlocked tests the line and two parallel ones to each side.
constexpr std::int32_t SightSpread = 2;

std::int64_t locationOf(std::int32_t whole) {
  return static_cast<std::int64_t>(whole) << FractionBits;
}

std::int32_t wholeOf(std::int64_t location) {
  return static_cast<std::int32_t>(location >> FractionBits);
}

// A step of the clip along the rotation, in 1/32768ths (calculatePosMatrix's RotMatrix of the
// root's rotation, applied to the momentum: the model faces its -z in the original's axes).
OriginalPoint turnedStep(RootStep step, std::int32_t rotation) {
  const std::int64_t sine = core::original::rsin(rotation);
  const std::int64_t cosine = core::original::rcos(rotation);
  const std::int64_t side = step.side;
  const std::int64_t back = -static_cast<std::int64_t>(step.forward);
  return {.x = static_cast<std::int32_t>(((cosine * side) + (sine * back)) << MatrixToFraction),
          .z = static_cast<std::int32_t>(((-sine * side) + (cosine * back)) << MatrixToFraction)};
}

// The original's integer division (toward zero), for its cell maths.
std::int32_t cellIndex(std::int32_t value) {
  return value / CellSize;
}

// map.c: isFiveTileWidePathBlocked: the line between two cells, or one of the lines beside it.
bool sightBlocked(const assets::MapCollision& map, CollisionCell from, CollisionCell to) {
  const bool alongX = std::abs(to.column - from.column) >= std::abs(to.row - from.row);
  for (std::int32_t offset = -SightSpread; offset <= SightSpread; ++offset) {
    const CollisionCell shift = alongX ? CollisionCell{.column = 0, .row = offset}
                                       : CollisionCell{.column = offset, .row = 0};
    if (isCellLineBlocked(map, {.column = from.column + shift.column, .row = from.row + shift.row},
                          {.column = to.column + shift.column, .row = to.row + shift.row})) {
      return true;
    }
  }
  return false;
}

// map.c: isRectInRect: the segment from (left, top) to (right, bottom) against a square of
// half-side `half` around `centre`; touching edges count.
bool segmentInSquare(OriginalPoint centre, std::int32_t half, std::int32_t left, std::int32_t top,
                     std::int32_t right, std::int32_t bottom) {
  return centre.x + half >= left && right >= centre.x - half && top >= centre.z - half &&
         bottom <= centre.z + half;
}

} // namespace

PartnerFollow::PartnerFollow(std::int32_t radius, assets::ClipMotions clips)
    : radius_(radius), clips_(std::move(clips)) {}

CollisionCell PartnerFollow::cellOf(OriginalPoint point) {
  CollisionCell cell{.column = cellIndex(point.x) + GridOrigin,
                     .row = GridOrigin - cellIndex(point.z)};
  if (point.x < 0) {
    --cell.column;
  }
  if (point.z > 0) {
    --cell.row;
  }
  return cell;
}

OriginalPoint PartnerFollow::centreOf(CollisionCell cell) {
  return {.x = ((cell.column - GridOrigin) * CellSize) + (CellSize / 2),
          .z = ((GridOrigin - cell.row) * CellSize) - (CellSize / 2)};
}

std::int32_t PartnerFollow::closeness(OriginalPoint player) const {
  const std::int64_t dx = player.x - position_.x;
  const std::int64_t dz = player.z - position_.z;
  const std::int64_t distance = (dx * dx) + (dz * dz);
  const std::int64_t walk = radius_ * 5 / 2;
  const std::int64_t run = radius_ * 7 / 2;
  if (distance > run * run) {
    return 0;
  }
  return distance >= walk * walk ? 1 : 2;
}

std::vector<CollisionCell> PartnerFollow::trail() const {
  std::vector<CollisionCell> cells;
  for (std::int32_t i = 0; i < trailCount_; ++i) {
    cells.push_back(trail_.at(static_cast<std::size_t>((trailCurrent_ + i) % TrailCapacity)));
  }
  return cells;
}

void PartnerFollow::place(OriginalPoint at, std::int32_t rotation) {
  position_ = at;
  rotation_ = rotation & AngleMask;
  locX_ = locationOf(at.x);
  locZ_ = locationOf(at.z);
  standing_ = 0;
  stopFrames_ = 0;
  wanted_ = PartnerIdleClip;
  startClip(PartnerIdleClip);
}

void PartnerFollow::playFrom(std::int32_t clip, std::size_t frame, OriginalPoint fraction) {
  wanted_ = clip;
  startClip(clip);
  locX_ = locationOf(position_.x) + fraction.x;
  locZ_ = locationOf(position_.z) + fraction.z;
  for (std::size_t i = 0; i < frame && clip_.running; ++i) {
    // Runs the clip's instructions up to that frame without moving.
    clip_.held = HeldStep;
    tickClip();
  }
  clip_.held = 0;
}

void PartnerFollow::standHeld() {
  startClip(PartnerIdleClip);
}

void PartnerFollow::forgetTrail(OriginalPoint player) {
  trailCurrent_ = 0;
  trailCount_ = 0;
  trail_.fill({});
  playerCell_ = cellOf(player);
}

const assets::ClipMotion* PartnerFollow::motionOf(std::int32_t id) const {
  const auto found = clips_.find(id);
  return found == clips_.end() ? nullptr : &found->second;
}

std::size_t PartnerFollow::clipLength(std::int32_t id) const {
  const assets::ClipMotion* motion = motionOf(id);
  return motion == nullptr ? 1 : motion->length();
}

void PartnerFollow::startClip(std::int32_t id) {
  // anim.c: startAnimation: frame 1, no momentum, animFlag 1 (the grace's bits cleared), loopCount
  // 0, and the location's fraction dropped.
  const std::uint32_t starts = clip_.starts + 1;
  clip_ = Playing{};
  clip_.id = id;
  clip_.starts = starts;
  locX_ = locationOf(position_.x);
  locZ_ = locationOf(position_.z);
}

void PartnerFollow::tickClip() {
  // anim.c: tickAnimation: an ended clip neither moves nor goes on.
  if (!clip_.running) {
    return;
  }
  if ((clip_.held & HeldStep) == 0) {
    // applyRootMomentum, with the sides collisionGrace found blocked cancelled.
    OriginalPoint step = turnedStep(clip_.momentum, rotation_);
    if (((clip_.held & NoStepTowardMinusZ) != 0 && step.z < 0) ||
        ((clip_.held & NoStepTowardPlusZ) != 0 && step.z > 0)) {
      step.z = 0;
    }
    if (((clip_.held & NoStepTowardMinusX) != 0 && step.x < 0) ||
        ((clip_.held & NoStepTowardPlusX) != 0 && step.x > 0)) {
      step.x = 0;
    }
    locX_ += step.x;
    locZ_ += step.z;
    position_ = {.x = wholeOf(locX_), .z = wholeOf(locZ_)};
  }
  // The clip's instructions for this frame: the next step, and its loop. The extracted clip ends a
  // forever loop on its last frame (formats: animation.cpp), so a pass ends there.
  const assets::ClipMotion* motion = motionOf(clip_.id);
  if (motion == nullptr || motion->steps.empty()) {
    clip_.running = false;
    return;
  }
  const std::size_t last = motion->length() - 1;
  if (clip_.frame == 0 && motion->repeatFrom) {
    clip_.loops = Playing::Loops::Forever;
  }
  std::size_t next = clip_.frame + 1;
  if (clip_.frame >= last) {
    if (clip_.loops == Playing::Loops::Forever && motion->repeatFrom) {
      next = *motion->repeatFrom;
    } else {
      // The last pass ended (loopCount 0) or the clip has no loop: animFrame reaches frameCount.
      clip_.loops = Playing::Loops::None;
      clip_.running = false;
      clip_.frame = next;
      return;
    }
  }
  clip_.frame = next;
  clip_.momentum = motion->stepAt(next).value_or(RootStep{});
}

bool PartnerFollow::mapBlocks(const assets::MapCollision& map, OriginalPoint at,
                              OriginalPoint step) const {
  // map.c: checkMapCollision (map_collision.c: checkMapCollisionX/Z): the leading edge of the
  // partner's square, half its radius to each side, on each axis it moves along.
  const std::int32_t half = radius_ / 2;
  const auto rowOf = [](std::int32_t z) {
    std::int32_t row = (GridOrigin - 1) - cellIndex(z);
    if (z < 0) {
      ++row;
    }
    return row;
  };
  const auto columnOf = [](std::int32_t x) {
    std::int32_t column = cellIndex(x) + GridOrigin;
    if (x < 0) {
      --column;
    }
    return column;
  };
  const auto blocked = [&](std::int32_t column, std::int32_t row) {
    return (map.value({.column = column, .row = row}) & assets::MapCollision::BlockedBit) != 0;
  };
  if (step.z != 0) {
    const std::int32_t row = rowOf(step.z > 0 ? at.z + radius_ : at.z - radius_);
    for (std::int32_t column = columnOf(at.x - half); column <= columnOf(at.x + half); ++column) {
      if (blocked(column, row)) {
        return true;
      }
    }
  }
  if (step.x != 0) {
    const std::int32_t column = columnOf(step.x < 0 ? at.x - radius_ : at.x + radius_);
    for (std::int32_t row = rowOf(at.z + half); row <= rowOf(at.z - half); ++row) {
      if (blocked(column, row)) {
        return true;
      }
    }
  }
  return false;
}

PartnerFollow::Blocker PartnerFollow::checkStep(const Frame& frame, bool pastPlayer) const {
  // map.c: entityCheckCollision: the clip's next step forward (entityMoveForward; its side part is
  // not tested), from the location with its fraction.
  const OriginalPoint turned =
      turnedStep({.side = 0, .forward = clip_.momentum.forward}, rotation_);
  const OriginalPoint to{.x = wholeOf(locX_ + turned.x), .z = wholeOf(locZ_ + turned.z)};
  OriginalPoint step{.x = to.x - position_.x, .z = to.z - position_.z};
  if (rotation_ == QuarterTurn || rotation_ == ThreeQuarters) {
    step.z = 0;
  }
  if (rotation_ == 0 || rotation_ == HalfTurn) {
    step.x = 0;
  }
  if (frame.map != nullptr && mapBlocks(*frame.map, to, step)) {
    return Blocker::Map;
  }
  if (!pastPlayer) {
    // entityCheckEntityCollision: the leading edges of the partner's square against the player's.
    const std::int32_t left = to.x - radius_;
    const std::int32_t right = to.x + radius_;
    const std::int32_t near = to.z + radius_;
    const std::int32_t far = to.z - radius_;
    const std::int32_t half = frame.playerRadius;
    if ((step.x < 0 && segmentInSquare(frame.player, half, left, near, left, far)) ||
        (step.x > 0 && segmentInSquare(frame.player, half, right, near, right, far)) ||
        (step.z < 0 && segmentInSquare(frame.player, half, left, far, right, far)) ||
        (step.z > 0 && segmentInSquare(frame.player, half, left, near, right, near))) {
      return Blocker::Player;
    }
  }
  if ((step.x != 0 || step.z != 0) && frame.npcBlocks && frame.npcBlocks(position_, to)) {
    return Blocker::Npc;
  }
  return Blocker::None;
}

void PartnerFollow::collisionGrace(const Frame& frame) {
  // map.c: collisionGrace(player, partner): past the player, the first free cardinal rotation of
  // the partner's eighth of a turn turns it at most 0x50 toward it; the blocked ones before it
  // cancel the step toward them. With none free it turns 32 and holds its step.
  clip_.held = 0;
  if (checkStep(frame, true) == Blocker::None) {
    return;
  }
  const std::int32_t start = rotation_;
  std::uint8_t sides = 0;
  for (const std::int32_t rotation :
       GraceRotations.at(static_cast<std::size_t>(start / (HalfTurn / 4)))) {
    rotation_ = rotation;
    if (checkStep(frame, true) != Blocker::None) {
      sides = static_cast<std::uint8_t>(sides | (NoStepTowardMinusZ << (rotation / QuarterTurn)));
      continue;
    }
    const std::int32_t clockwise = (start - rotation + Turn) & AngleMask;
    const std::int32_t counter = (rotation - start + Turn) & AngleMask;
    if (clockwise < counter) {
      rotation_ = (start - std::min(clockwise, GraceTurn) + Turn) & AngleMask;
    } else {
      rotation_ = (start + std::min(counter, GraceTurn)) & AngleMask;
    }
    clip_.held = sides;
    return;
  }
  rotation_ = (start + StuckTurn) & AngleMask;
  clip_.held = HeldStep;
}

void PartnerFollow::tickTrail(const Frame& frame) {
  // map.c: partnerTickCollision: when the player changes cell out of the partner's sight, its last
  // cell joins the trail, unless the newest one is still in sight; in sight, no trail.
  const CollisionCell player = cellOf(frame.player);
  const auto at = [&](std::int32_t index) -> CollisionCell& {
    return trail_.at(static_cast<std::size_t>(index % TrailCapacity));
  };
  if (player != playerCell_ && frame.map != nullptr) {
    if (sightBlocked(*frame.map, player, cellOf(position_))) {
      if (trailCount_ != 0) {
        if (sightBlocked(*frame.map, player, at(trailCurrent_ + trailCount_ - 1))) {
          at(trailCurrent_ + trailCount_) = playerCell_;
          ++trailCount_;
        }
      } else {
        // initializePartnerWaypoint takes the player's cell as the last one seen first.
        forgetTrail(frame.player);
        at(trailCurrent_) = playerCell_;
        ++trailCount_;
      }
    } else {
      forgetTrail(frame.player);
    }
  }
  // The first of the second, third and fourth newest cells in the player's sight makes the newer
  // ones unnecessary.
  if (trailCount_ >= 2 && frame.map != nullptr) {
    for (std::int32_t back = 0; back < 3; ++back) {
      const std::int32_t index = (trailCurrent_ + trailCount_ - 2 - back) % TrailCapacity;
      if (!sightBlocked(*frame.map, player, at(index))) {
        trailCount_ -= back + 1;
        break;
      }
      if (index == trailCurrent_) {
        break;
      }
    }
  }
  playerCell_ = player;
}

void PartnerFollow::tickBattleTrail(const assets::MapCollision& map, OriginalPoint player,
                                    OriginalPoint partner) {
  const CollisionCell cell = cellOf(partner);
  const auto at = [&](std::int32_t index) -> CollisionCell& {
    return trail_.at(static_cast<std::size_t>(index % TrailCapacity));
  };
  if (cell != battleCell_) {
    if (!sightBlocked(map, cellOf(player), cell)) {
      forgetTrail(player);
    } else if (trailCount_ == 0) {
      forgetTrail(player);
      at(trailCurrent_) = battleCell_;
      ++trailCount_;
    } else if (sightBlocked(map, cell, at(trailCurrent_))) {
      // The trail moves back one place and the left cell becomes its head; a full trail drops its
      // newest cell (the original's ring of 30 wraps onto it).
      for (std::int32_t n = std::min(trailCount_, TrailCapacity - 1); n > 0; --n) {
        at(trailCurrent_ + n) = at(trailCurrent_ + n - 1);
      }
      at(trailCurrent_) = battleCell_;
      trailCount_ = std::min(trailCount_ + 1, TrailCapacity);
    }
  }
  battleCell_ = cell;
}

void PartnerFollow::setIdle(const PartnerMood& mood) {
  // partner.c: setPartnerIdle (from the trial version on, it also starts again a partner that was
  // not standing still).
  if ((wanted_ != PartnerIdleClip && wanted_ != PartnerIdleTiredClip) || standing_ != 2) {
    wanted_ = mood.tired ? PartnerIdleTiredClip : PartnerIdleClip;
    startClip(wanted_);
  }
}

void PartnerFollow::setSlowWalking(const PartnerMood& mood) {
  wanted_ = mood.tired ? PartnerWalkTiredClip : PartnerWalkClip;
  startClip(wanted_);
}

void PartnerFollow::updateConditionAnimation(const PartnerMood& mood) {
  if (mood.gesture) {
    wanted_ = *mood.gesture;
  } else {
    setIdle(mood);
  }
}

void PartnerFollow::pickEmotionWait(const Frame& frame) {
  emotionPlays_ = (frame.randomBelow ? frame.randomBelow(gesturePlays_) : 0) + 1;
}

void PartnerFollow::tickWalking(const Frame& frame) {
  // partner.c: partnerTickWalking.
  const PartnerMood& mood = frame.mood;
  const auto walking = [&] {
    return clip_.id == PartnerWalkClip || clip_.id == PartnerWalkTiredClip;
  };
  const auto standing = [&] {
    return clip_.id == PartnerIdleClip || clip_.id == PartnerIdleTiredClip;
  };
  const auto finishPass = [&] {
    if (clip_.loops == Playing::Loops::Forever) {
      clip_.loops = Playing::Loops::LastPass;
    }
  };
  const auto loopsDone = [&] { return clip_.loops == Playing::Loops::None; };
  switch (closeness(frame.player)) {
  case 0:
    if (walking() && !mood.unhappy) {
      finishPass();
      if (loopsDone()) {
        wanted_ = PartnerRunClip;
      }
    } else if (clip_.id != PartnerRunClip && !mood.unhappy) {
      wanted_ = PartnerRunClip;
    } else {
      wanted_ = mood.unhappy ? PartnerWalkClip : PartnerRunClip;
      if (wanted_ != clip_.id) {
        startClip(wanted_);
      }
    }
    standing_ = 1;
    break;
  case 1:
    if (clip_.id == PartnerRunClip) {
      setSlowWalking(mood);
    } else if (!walking()) {
      finishPass();
      if (!clip_.running) {
        setSlowWalking(mood);
      }
    } else if (loopsDone()) {
      setSlowWalking(mood);
    }
    emotionPlays_ = -1;
    standing_ = 1;
    break;
  default: {
    if (standing() && standing_ != 2) {
      updateConditionAnimation(mood);
    }
    if (clip_.id > PartnerIdleTiredClip && clip_.id <= PartnerRunClip) {
      clip_.held = 0;
      const Blocker blocker = checkStep(frame, false);
      finishPass();
      if (loopsDone() || blocker == Blocker::Player) {
        pickEmotionWait(frame);
        setIdle(mood);
        stopFrames_ = 0;
      }
    }
    if (standing() &&
        stopFrames_ > static_cast<std::int32_t>(clipLength(clip_.id)) * emotionPlays_) {
      updateConditionAnimation(mood);
    }
    if (!standing() && !clip_.running) {
      pickEmotionWait(frame);
      setIdle(mood);
      stopFrames_ = 0;
    }
    ++stopFrames_;
    standing_ = 2;
    break;
  }
  }
  if (wanted_ != clip_.id) {
    startClip(wanted_);
    stopFrames_ = 0;
  }
}

void PartnerFollow::tick(const Frame& frame) {
  // map.c: partnerTickCollision.
  tickTrail(frame);
  const std::int32_t before = rotation_;
  if (trailCount_ != 0) {
    const CollisionCell target = trail_.at(static_cast<std::size_t>(trailCurrent_));
    const OriginalPoint centre = centreOf(target);
    rotation_ = core::original::atan4096(centre.z - position_.z, centre.x - position_.x);
    if (cellOf(position_) == target) {
      --trailCount_;
      trailCurrent_ = (trailCurrent_ + 1) % TrailCapacity;
    }
  } else {
    rotation_ =
        core::original::atan4096(frame.player.z - position_.z, frame.player.x - position_.x);
  }
  clip_.held = 0;
  const Blocker blocker = checkStep(frame, false);
  if (blocker == Blocker::Player || blocker == Blocker::Npc) {
    // Against the player or an NPC (not a wall): the facing it had, and the grace past the
    // player.
    rotation_ = before;
    collisionGrace(frame);
  }
  tickWalking(frame);
  // partnerTickOverworld: the clip after the partner's state.
  tickClip();
}

} // namespace dwe::game::field
