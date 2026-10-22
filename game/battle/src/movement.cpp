#include <algorithm>
#include <cstdint>

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

// Keeps a point inside the arena (the original's map collision; Hueco).
Point clampToArena(const BattleContext& context, Point point) {
  const Point center = context.state->arenaCenter;
  const std::int64_t radius = context.rules->timing.arenaRadius;
  const std::int64_t distance = integerSqrt(distanceSquared(point, center));
  if (distance <= radius) {
    return point;
  }
  const std::int64_t dx = static_cast<std::int64_t>(point.x) - center.x;
  const std::int64_t dz = static_cast<std::int64_t>(point.z) - center.z;
  return {.x = center.x + static_cast<std::int32_t>(dx * radius / distance),
          .z = center.z + static_cast<std::int32_t>(dz * radius / distance)};
}

// Moves a fighter `step` units along its facing (backwards when negative).
void moveAlongFacing(BattleContext& context, Fighter& fighter, std::int32_t step) {
  const Point next{.x = fighter.position.x +
                        static_cast<std::int32_t>(static_cast<std::int64_t>(fighter.facing.x) *
                                                  step / FacingScale),
                   .z = fighter.position.z +
                        static_cast<std::int32_t>(static_cast<std::int64_t>(fighter.facing.z) *
                                                  step / FacingScale)};
  fighter.position = clampToArena(context, next);
  fighter.pose = Pose::Walking;
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

void walkToward(BattleContext& context, FighterId id, Point point) {
  Fighter& fighter = context.fighter(id);
  lookAt(fighter, point);
  const auto distance = integerSqrt(distanceSquared(fighter.position, point));
  moveAlongFacing(
      context, fighter,
      static_cast<std::int32_t>(std::min<std::int64_t>(context.rules->timing.walkSpeed, distance)));
}

void backAwayFrom(BattleContext& context, FighterId id, Point point) {
  Fighter& fighter = context.fighter(id);
  lookAt(fighter, point);
  moveAlongFacing(context, fighter, -context.rules->timing.walkSpeed);
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
    backAwayFrom(context, id, other.position);
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
  moveAlongFacing(context, context.fighter(id), context.rules->timing.walkSpeed);
}

bool isWalking(const Fighter& fighter) {
  return fighter.pose == Pose::Walking;
}

} // namespace dwe::game::battle
