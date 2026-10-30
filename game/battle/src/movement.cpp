#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "battle_context.hpp"

namespace dwe::game::battle {

namespace {

// The largest turn of a confused fighter, in the original's angle units (4096 a turn): rand(0x400)
// - 0x200.
constexpr std::int32_t ConfusedTurnSpan = 0x400;
constexpr std::int32_t AngleUnitsPerTurn = 4096;
constexpr double Tau = 6.283185307179586;

} // namespace

std::int64_t integerSqrt(std::int64_t value) {
  if (value <= 0) {
    return 0;
  }
  std::int64_t root = 0;
  std::int64_t bit = std::int64_t{1} << 62;
  while (bit > value) {
    bit >>= 2;
  }
  while (bit != 0) {
    if (value >= root + bit) {
      value -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
    bit >>= 2;
  }
  return root;
}

namespace {

// Sine and cosine for angles up to an eighth of a turn, by their series: plain arithmetic, so every
// platform gets the same result.
double sineOf(double x) {
  const double x2 = x * x;
  return x * (1.0 - ((x2 / 6.0) * (1.0 - ((x2 / 20.0) * (1.0 - (x2 / 42.0))))));
}

double cosineOf(double x) {
  const double x2 = x * x;
  return 1.0 - ((x2 / 2.0) * (1.0 - ((x2 / 12.0) * (1.0 - ((x2 / 30.0) * (1.0 - (x2 / 56.0)))))));
}

// The original's angles: 4096 units a turn, 0 facing its +Z (world -Z), growing toward +X.
constexpr std::int32_t AngleMask = AngleUnitsPerTurn - 1;
constexpr std::int32_t QuarterTurn = AngleUnitsPerTurn / 4;
constexpr std::int32_t HalfTurn = AngleUnitsPerTurn / 2;
constexpr std::int32_t EighthTurn = AngleUnitsPerTurn / 8;
// Below tan(pi / 8) the arctangent series converges fast enough.
constexpr double TanEighthPi = 0.41421356237309503;
constexpr int ArcTangentTerms = 12;

std::int32_t wrapAngle(std::int32_t angle) {
  return angle & AngleMask;
}

// The facing of an angle (forward is (sin, cos) in the original's axes, (sin, -cos) in world axes).
Facing facingOf(std::int32_t angle) {
  angle = wrapAngle(angle);
  const std::int32_t rest = angle % QuarterTurn;
  double sine = 0.0;
  double cosine = 0.0;
  if (rest <= EighthTurn) {
    const double x = Tau * rest / AngleUnitsPerTurn;
    sine = sineOf(x);
    cosine = cosineOf(x);
  } else {
    const double x = Tau * (QuarterTurn - rest) / AngleUnitsPerTurn;
    sine = cosineOf(x);
    cosine = sineOf(x);
  }
  switch (angle / QuarterTurn) {
  case 1:
    std::tie(sine, cosine) = std::pair{cosine, -sine};
    break;
  case 2:
    std::tie(sine, cosine) = std::pair{-sine, -cosine};
    break;
  case 3:
    std::tie(sine, cosine) = std::pair{-cosine, sine};
    break;
  default:
    break;
  }
  return {.x = static_cast<std::int32_t>(std::lround(sine * FacingScale)),
          .z = static_cast<std::int32_t>(std::lround(-cosine * FacingScale))};
}

double arcTangentSeries(double t) {
  const double t2 = t * t;
  double power = t;
  double sum = 0.0;
  for (int k = 0; k < ArcTangentTerms; ++k) {
    sum += (k % 2 == 0 ? power : -power) / ((2.0 * k) + 1.0);
    power *= t2;
  }
  return sum;
}

// The arctangent of t in [0, 1], by its series: plain arithmetic, like sineOf.
double arcTangent(double t) {
  if (t > TanEighthPi) {
    return (Tau / 8.0) + arcTangentSeries((t - 1.0) / (t + 1.0));
  }
  return arcTangentSeries(t);
}

// The angle of a facing (map.c: entityLookAtLocation's _atan).
std::int32_t angleOf(Facing facing) {
  const double sine = facing.x;
  const double cosine = -static_cast<double>(facing.z);
  if (sine == 0.0 && cosine == 0.0) {
    return 0;
  }
  const double s = std::abs(sine);
  const double c = std::abs(cosine);
  double angle = s <= c ? arcTangent(s / c) : (Tau / 4.0) - arcTangent(c / s);
  if (cosine < 0.0) {
    angle = (Tau / 2.0) - angle;
  }
  if (sine < 0.0) {
    angle = -angle;
  }
  return wrapAngle(static_cast<std::int32_t>(std::lround(angle * AngleUnitsPerTurn / Tau)));
}

// Turns from `from` toward `to` by at most `limit`, the shorter way (counterclockwise on a tie).
std::int32_t turnToward(std::int32_t from, std::int32_t to, std::int32_t limit) {
  std::int32_t turn = wrapAngle(to - from);
  if (turn > HalfTurn) {
    turn -= AngleUnitsPerTurn;
  }
  return wrapAngle(from + std::clamp(turn, -limit, limit));
}

Point stepFrom(Point point, Facing facing, std::int32_t step) {
  return {.x = point.x +
               static_cast<std::int32_t>(static_cast<std::int64_t>(facing.x) * step / FacingScale),
          .z = point.z +
               static_cast<std::int32_t>(static_cast<std::int64_t>(facing.z) * step / FacingScale)};
}

// What stops a step (map.c: entityCheckCollision returns 11, 10 or the entity's index).
enum class Blocker : std::uint8_t { None, CombatArea, Map, Fighter };

// Without a ground, the combat area is the ruleset's arena radius around where the fighters
// started: a step may not go further out past it.
bool leavesArena(const BattleContext& context, Point from, Point to) {
  const Point center = context.state->arenaCenter;
  const std::int64_t radius = context.rules->timing.arenaRadius;
  const std::int64_t after = distanceSquared(to, center);
  return after > radius * radius && after > distanceSquared(from, center);
}

// map.c: hasMovedOutsideCombatArea: past the rectangle around the screen's centre and moving
// further out on either axis.
bool movesOutOfArea(ScreenPoint before, ScreenPoint after, std::int32_t width,
                    std::int32_t height) {
  const std::int32_t halfWidth = width / 2;
  const std::int32_t halfHeight = height / 2;
  return (after.x < -halfWidth && after.x < before.x) ||
         (after.x > halfWidth && after.x > before.x) ||
         (after.y < -halfHeight && after.y < before.y) ||
         (after.y > halfHeight && after.y > before.y);
}

} // namespace

// map.c: entityCheckCombatArea: the step leaves a rectangle of the screen. Two corners of the
// mover's box are tested, as its loop over QUADRANTS ignores its inner index: the near left one on
// the ground and the near right one corner_height up.
bool leavesScreenArea(const BattleGround& ground, const TimingRules& timing, Point from, Point to,
                      std::int32_t radius, std::int32_t width, std::int32_t height) {
  const std::array<std::pair<std::int32_t, std::int32_t>, 2> corners{
      {{-radius, 0}, {radius, timing.combatAreaCornerHeight}}};
  return std::ranges::any_of(corners, [&](const std::pair<std::int32_t, std::int32_t>& corner) {
    const auto [side, lift] = corner;
    const std::optional<ScreenPoint> before =
        ground.screenPointOf({.x = from.x + side, .z = from.z - radius}, lift);
    const std::optional<ScreenPoint> after =
        ground.screenPointOf({.x = to.x + side, .z = to.z - radius}, lift);
    return before && after && movesOutOfArea(*before, *after, width, height);
  });
}

namespace {

// The fighters' combat area (entityCheckCollision with the battle's width and height).
bool leavesCombatArea(const BattleContext& context, Point from, Point to, std::int32_t radius) {
  if (context.ground == nullptr) {
    return leavesArena(context, from, to);
  }
  const TimingRules& timing = context.rules->timing;
  return leavesScreenArea(*context.ground, timing, from, to, radius, timing.combatAreaWidth,
                          timing.combatAreaHeight);
}

// The leading edge of the mover's square, on each axis it moves along, against the square of
// half-side `reach` around `at` (map.c: entityCheckEntityCollision in battle, isRectInRect; edges
// touching count).
bool runsInto(Point from, Point to, std::int32_t radius, Point at, std::int64_t reach) {
  const std::int64_t west = at.x - reach;
  const std::int64_t east = at.x + reach;
  const std::int64_t north = at.z - reach;
  const std::int64_t south = at.z + reach;
  const auto overlaps = [&](std::int64_t x0, std::int64_t x1, std::int64_t z0, std::int64_t z1) {
    return x0 <= east && x1 >= west && z0 <= south && z1 >= north;
  };
  const std::int64_t left = static_cast<std::int64_t>(to.x) - radius;
  const std::int64_t right = static_cast<std::int64_t>(to.x) + radius;
  const std::int64_t near = static_cast<std::int64_t>(to.z) - radius;
  const std::int64_t far = static_cast<std::int64_t>(to.z) + radius;
  return (to.x < from.x && overlaps(left, left, near, far)) ||
         (to.x > from.x && overlaps(right, right, near, far)) ||
         (to.z < from.z && overlaps(left, right, near, near)) ||
         (to.z > from.z && overlaps(left, right, far, far));
}

// The walk clips (map.c: entityCheckCollision lets them through the player in battle).
bool walkClip(std::int32_t clip) {
  return clip == WalkClip || clip == WeakWalkClip;
}

// How far the step entityCheckCollision tests goes along the facing (map.c: entityMoveForward: the
// clip's next root step); the ruleset's walk speed for a fighter without clips.
std::int32_t stepAhead(const BattleContext& context, const Fighter& fighter) {
  if (!fighter.clips) {
    return context.rules->timing.walkSpeed;
  }
  return nextRootStep(fighter).value_or(RootStep{}).forward;
}

// What stops a fighter's next step if it faces `facing` (map.c: entityCheckCollision): the combat
// area, the map, then the other fighters still standing and, `withPlayer`, the player (a step of a
// clip other than a walk).
Blocker blockerAhead(const BattleContext& context, FighterId id, Facing facing,
                     bool withPlayer = false) {
  const Fighter& fighter = context.fighter(id);
  const std::int32_t radius = fighter.combatant.radius;
  const Point from = fighter.position;
  const Point to = stepFrom(from, facing, stepAhead(context, fighter));
  if (leavesCombatArea(context, from, to, radius)) {
    return Blocker::CombatArea;
  }
  if (context.ground != nullptr && context.ground->blocksStep(from, to, radius)) {
    return Blocker::Map;
  }
  const std::vector<Fighter>& fighters = context.state->fighters;
  for (FighterId other = 0; other < fighters.size(); ++other) {
    const Fighter& them = fighters[other];
    if (other != id && them.hp > 0 &&
        runsInto(from, to, radius, them.position, them.combatant.radius)) {
      return Blocker::Fighter;
    }
  }
  if (withPlayer && !context.state->tamerless &&
      runsInto(from, to, radius, context.state->tamer, context.rules->timing.retreatRadius)) {
    return Blocker::Fighter;
  }
  return Blocker::None;
}

// map.c: entityCheckCollision on the fighter itself: what stops its next step facing `facing`.
// The test lets the fighter's root step go again (it clears animFlag bit 1): a fighter that turns
// away from what blocked it walks on the same frame.
Blocker checkStep(BattleContext& context, FighterId id, Facing facing) {
  context.fighter(id).rootHeld = false;
  return blockerAhead(context, id, facing);
}

bool freeAt(BattleContext& context, FighterId id, std::int32_t angle) {
  return checkStep(context, id, facingOf(angle)) == Blocker::None;
}

// map.c: COLLISION_GRACE_ROTATION: where a blocked fighter tries to turn, by the eighth of a turn
// it faces.
constexpr std::array<std::array<std::int32_t, 4>, 8> GraceTurns{{{0, 0x400, 0xc00, 0x800},
                                                                 {0x400, 0, 0x800, 0xc00},
                                                                 {0x400, 0x800, 0, 0xc00},
                                                                 {0x800, 0x400, 0xc00, 0},
                                                                 {0x800, 0xc00, 0x400, 0},
                                                                 {0xc00, 0x800, 0, 0x400},
                                                                 {0xc00, 0, 0x800, 0x400},
                                                                 {0, 0xc00, 0x400, 0x800}}};
// collisionGrace: the largest turn a frame toward a free way, and the nudge when there is none.
constexpr std::int32_t GraceTurnStep = 0x50;
constexpr std::int32_t GraceNudge = 32;

// map.c: collisionGrace: a blocked fighter turns a little toward the first free way of its
// eighth of a turn, or nudges its facing when every way is blocked.
void collisionGrace(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (checkStep(context, id, fighter.facing) == Blocker::None) {
    return;
  }
  const std::int32_t start = angleOf(fighter.facing);
  for (const std::int32_t way :
       GraceTurns.at(static_cast<std::size_t>(start / (QuarterTurn / 2)))) {
    if (freeAt(context, id, way)) {
      fighter.facing = facingOf(turnToward(start, way, GraceTurnStep));
      return;
    }
  }
  fighter.facing = facingOf(start + GraceNudge);
  fighter.rootHeld = true;
}

// BTL_findUnblockedRotation: the sweeps of three eighths tried from the quarter a fighter faces.
constexpr std::int32_t SweepSteps = 3;

// battle_main.c: BTL_findUnblockedRotation: a fighter that would leave the combat area looks for a
// free sweep of three eighths of a turn from the quarter it faces and turns somewhere in it; any
// other block, or no free sweep, goes back to `original` and the grace turn.
void findUnblockedTurn(BattleContext& context, FighterId id, Blocker hit, std::int32_t original) {
  Fighter& fighter = context.fighter(id);
  const auto grace = [&] {
    fighter.facing = facingOf(original);
    collisionGrace(context, id);
  };
  if (hit != Blocker::CombatArea) {
    grace();
    return;
  }
  const std::int32_t quarter = (angleOf(fighter.facing) / QuarterTurn) * QuarterTurn;
  std::int32_t sweep = quarter + HalfTurn;
  bool fromFree = true;
  if (freeAt(context, id, quarter)) {
    sweep = quarter + HalfTurn + QuarterTurn;
  } else if (freeAt(context, id, quarter + QuarterTurn)) {
    sweep = quarter + QuarterTurn;
  } else {
    fromFree = false;
  }
  std::int32_t step = 0;
  while (step < SweepSteps && freeAt(context, id, sweep + (step * QuarterTurn / 2))) {
    ++step;
  }
  if (step != SweepSteps) {
    grace();
    return;
  }
  if (!fromFree && freeAt(context, id, original + HalfTurn)) {
    fighter.facing = facingOf(original + HalfTurn);
    return;
  }
  const std::int32_t turn = wrapAngle(sweep + context.random->below(QuarterTurn));
  if (freeAt(context, id, turn)) {
    fighter.facing = facingOf(turn);
    return;
  }
  grace();
}

// The largest turn a frame of a fighter that backs away after a collision (BTL_backAwayFromTarget).
constexpr std::int32_t BackingTurnStep = 20;

// battle_main.c: BTL_backAwayFromTarget: a fighter turns its back on its target. Once a step
// backing away hit something it turns gradually instead: sideways when it faces the way the target
// does, else toward its back; failing that it keeps its facing and looks for a free way.
void turnAway(BattleContext& context, FighterId id, std::int32_t original, std::int32_t away,
              std::int32_t targetAngle) {
  Fighter& fighter = context.fighter(id);
  if (!fighter.collidedWhileBacking) {
    fighter.facing = facingOf(away);
    const Blocker hit = checkStep(context, id, fighter.facing);
    if (hit != Blocker::None) {
      fighter.collidedWhileBacking = true;
      findUnblockedTurn(context, id, hit, original);
    }
    return;
  }
  if (original == targetAngle) {
    for (const std::int32_t side : {original + QuarterTurn, original + HalfTurn + QuarterTurn}) {
      if (freeAt(context, id, side)) {
        fighter.facing = facingOf(side);
        return;
      }
    }
  } else {
    const std::int32_t turned = turnToward(original, away, BackingTurnStep);
    if (freeAt(context, id, turned)) {
      fighter.facing = facingOf(turned);
      return;
    }
  }
  fighter.facing = facingOf(original);
  const Blocker hit = checkStep(context, id, fighter.facing);
  if (hit != Blocker::None) {
    findUnblockedTurn(context, id, hit, original);
  }
}

// Moves a fighter `step` units along its facing if nothing stops the step (battle_main.c:
// BTL_tickFighterAction freezes the walk while entityCheckCollision finds something ahead). A
// fighter with clips walks by its walk clip's root motion instead (tickRootMotion), which the
// frame's step test holds; an ungated walk lets it go (BTL_tickMeleeAttack clears animFlag bit 1).
void moveAlongFacing(BattleContext& context, FighterId id, std::int32_t step, bool gated) {
  Fighter& fighter = context.fighter(id);
  fighter.pose = Pose::Walking;
  if (fighter.clips) {
    if (!gated) {
      fighter.rootHeld = false;
    }
    return;
  }
  if (gated && blockerAhead(context, id, fighter.facing) != Blocker::None) {
    return;
  }
  fighter.position = stepFrom(fighter.position, fighter.facing, step);
}

} // namespace

std::int64_t contactRangeSquared(const BattleContext& context, FighterId a, FighterId b) {
  const std::int64_t reach = static_cast<std::int64_t>(context.fighter(a).combatant.radius) +
                             context.fighter(b).combatant.radius;
  return reach * reach;
}

void lookAt(Fighter& fighter, Point point) {
  const std::int64_t dx = static_cast<std::int64_t>(point.x) - fighter.position.x;
  const std::int64_t dz = static_cast<std::int64_t>(point.z) - fighter.position.z;
  const std::int64_t length = integerSqrt((dx * dx) + (dz * dz));
  if (length == 0) {
    return;
  }
  fighter.facing = {.x = static_cast<std::int32_t>(dx * FacingScale / length),
                    .z = static_cast<std::int32_t>(dz * FacingScale / length)};
}

bool turnForHit(Fighter& fighter, Point attack) {
  const Facing toAttack{.x = attack.x - fighter.position.x, .z = attack.z - fighter.position.z};
  if (toAttack.x == 0 && toAttack.z == 0) {
    return false;
  }
  const std::int32_t facing = wrapAngle(angleOf(fighter.facing));
  const std::int32_t toward = wrapAngle(angleOf(toAttack));
  // The three cases of the original come to the attack within a quarter turn either way.
  const std::int32_t apart = wrapAngle(toward - facing + HalfTurn) - HalfTurn;
  const bool fromBehind = !fighter.status.flattened && std::abs(apart) > QuarterTurn;
  fighter.facing = facingOf(fromBehind ? toward + HalfTurn : toward);
  return fromBehind;
}

void walkToward(BattleContext& context, FighterId id, Point point) {
  Fighter& fighter = context.fighter(id);
  fighter.collidedWhileBacking = false;
  const Facing original = fighter.facing;
  lookAt(fighter, point);
  if (checkStep(context, id, fighter.facing) != Blocker::None) {
    fighter.facing = original;
    collisionGrace(context, id);
  }
  const auto distance = integerSqrt(distanceSquared(fighter.position, point));
  moveAlongFacing(
      context, id,
      static_cast<std::int32_t>(std::min<std::int64_t>(context.rules->timing.walkSpeed, distance)),
      true);
}

void approach(BattleContext& context, FighterId id, Point point) {
  Fighter& fighter = context.fighter(id);
  fighter.collidedWhileBacking = false;
  lookAt(fighter, point);
  const auto distance = integerSqrt(distanceSquared(fighter.position, point));
  moveAlongFacing(
      context, id,
      static_cast<std::int32_t>(std::min<std::int64_t>(context.rules->timing.walkSpeed, distance)),
      false);
}

void backAwayFrom(BattleContext& context, FighterId id, FighterId target) {
  Fighter& fighter = context.fighter(id);
  const Fighter& other = context.fighter(target);
  const std::int32_t original = angleOf(fighter.facing);
  lookAt(fighter, other.position);
  const std::int32_t away = wrapAngle(angleOf(fighter.facing) + HalfTurn);
  turnAway(context, id, original, away, angleOf(other.facing));
  moveAlongFacing(context, id, context.rules->timing.walkSpeed, true);
}

void keepDistance(BattleContext& context, FighterId id, FighterId target) {
  Fighter& fighter = context.fighter(id);
  const Fighter& other = context.fighter(target);
  const std::int64_t reach = static_cast<std::int64_t>(fighter.combatant.radius) +
                             other.combatant.radius + context.rules->spacing.contactMargin;
  const std::int64_t contact = reach * reach;
  const DistanceBand& band =
      isWalking(fighter) ? context.rules->spacing.walking : context.rules->spacing.standing;
  const std::int64_t distance = distanceSquared(fighter.position, other.position);
  if (distance < contact + band.nearSquared) {
    backAwayFrom(context, id, target);
  } else if (distance > contact + band.farSquared) {
    walkToward(context, id, other.position);
  } else {
    standIdle(fighter);
    lookAt(fighter, other.position);
  }
}

void turnAtRandom(BattleContext& context, Fighter& fighter) {
  const PartnerAiRules& rules = context.rules->partnerAi;
  if (context.random->below(rules.confusedTurnRoll) < rules.confusedTurnFrom) {
    return;
  }
  const std::int32_t units = context.random->below(ConfusedTurnSpan) - (ConfusedTurnSpan / 2);
  const double angle = Tau * units / AngleUnitsPerTurn;
  const double sine = sineOf(angle);
  const double cosine = cosineOf(angle);
  const double x = fighter.facing.x;
  const double z = fighter.facing.z;
  fighter.facing = {.x = static_cast<std::int32_t>((x * cosine) - (z * sine)),
                    .z = static_cast<std::int32_t>((x * sine) + (z * cosine))};
}

void walkForward(BattleContext& context, FighterId id) {
  moveAlongFacing(context, id, context.rules->timing.walkSpeed, true);
}

bool isWalking(const Fighter& fighter) {
  return fighter.pose == Pose::Walking;
}

void testRootStep(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (!fighter.clips) {
    return;
  }
  fighter.rootHeld =
      blockerAhead(context, id, fighter.facing, !walkClip(fighter.shown.clip)) != Blocker::None;
}

void tickRootMotion(BattleContext& context) {
  for (Fighter& fighter : context.state->fighters) {
    if (!fighter.clips || fighter.shown.frozen) {
      continue;
    }
    const auto clip = fighter.clips->find(fighter.shown.clip);
    if (clip == fighter.clips->end() || clip->second.ended(fighter.shown.frame)) {
      continue;
    }
    const std::optional<RootStep> step = clip->second.stepAt(fighter.shown.frame);
    ++fighter.shown.frame;
    if (!step || fighter.rootHeld) {
      continue;
    }
    // ApplyMatrixLV by the root's matrix: the model's x and z axes turned to the facing.
    const std::int64_t fx = fighter.facing.x;
    const std::int64_t fz = fighter.facing.z;
    const std::int64_t x = (step->side * fz) + (step->forward * fx) + fighter.rootRemainder.x;
    const std::int64_t z = (-step->side * fx) + (step->forward * fz) + fighter.rootRemainder.z;
    const auto whole = [](std::int64_t value) {
      return value >= 0 ? value / FacingScale : -((-value + FacingScale - 1) / FacingScale);
    };
    fighter.position.x += static_cast<std::int32_t>(whole(x));
    fighter.position.z += static_cast<std::int32_t>(whole(z));
    fighter.rootRemainder = {.x = static_cast<std::int32_t>(x - (whole(x) * FacingScale)),
                             .z = static_cast<std::int32_t>(z - (whole(z) * FacingScale))};
  }
}

} // namespace dwe::game::battle
