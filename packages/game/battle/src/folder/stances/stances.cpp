#include "folder/stances/stances.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "folder/fighters.hpp"
#include "folder/folder.hpp"
#include "mechanics/actions/actions.hpp"
#include "mechanics/decisions/decisions.hpp"
#include "mechanics/movement/movement.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;
// Distance keeps the fighter between this share of its preferred distance (squared) and all of it.
constexpr std::int64_t DistanceBandPercent = 80;
// Guard keeps within twice the contact reach of the ally it protects (four times squared).
constexpr std::int64_t GuardReachSquaredTimes = 4;

// The squared distance at which `move` reaches `target` from `id`.
std::int64_t reachOf(const BattleContext& context, FighterId id, FighterId target,
                     const BattleMove& move) {
  const std::int64_t contact = contactRangeSquared(context, id, target);
  return move.range == MoveRange::Melee ? contact : move.distanceSquared + contact;
}

} // namespace

void face(const BattleContext& context, FighterId id, FighterId target) {
  Fighter& fighter = context.fighter(id);
  standIdle(fighter);
  if (target != id) {
    lookAt(fighter, context.fighter(target).position);
  }
}

void moveByStance(BattleContext& context, FighterId id) {
  Fighter& self = context.fighter(id);
  if (!validFoe(context, id, self.target)) {
    setTarget(context, id, nearestFoe(context, id));
  }
  if (!self.target) {
    standIdle(self);
    return;
  }
  const FighterId target = *self.target;
  const std::int64_t distance = distanceSquared(self.position, context.fighter(target).position);
  const std::int64_t contact = contactRangeSquared(context, id, target);
  switch (folderFighter(context, id).effectiveStance()) {
  case Stance::Aggressive:
    if (distance > contact) {
      approach(context, id, target);
    } else {
      face(context, id, target);
    }
    return;
  case Stance::Hold:
    face(context, id, target);
    return;
  case Stance::Distance: {
    const std::int64_t reach = longestReach(context, id);
    const std::int64_t preferred = (reach > 0 ? reach : contact * 3) + contact;
    if (distance < preferred * DistanceBandPercent / Hundred) {
      backAwayFrom(context, id, target);
    } else if (distance > preferred) {
      walkToward(context, id, context.fighter(target).position);
    } else {
      face(context, id, target);
    }
    return;
  }
  case Stance::Guard: {
    std::optional<FighterId> ward;
    std::int64_t nearest = 0;
    for (const FighterId other : context.state->partnersIn()) {
      const std::int64_t away = distanceSquared(self.position, context.fighter(other).position);
      if (other != id && standing(context, other) && (!ward || away < nearest)) {
        ward = other;
        nearest = away;
      }
    }
    if (ward && nearest > contactRangeSquared(context, id, *ward) * GuardReachSquaredTimes) {
      walkToward(context, id, context.fighter(*ward).position);
    } else {
      face(context, id, target);
    }
    return;
  }
  }
}

bool inReach(const BattleContext& context, FighterId id, FighterId target, const BattleMove& move) {
  const std::int64_t reach = reachOf(context, id, target, move);
  const std::int64_t band = move.range == MoveRange::Melee
                                ? 0
                                : reach * context.rules->pacing.rangedBandPercent / Hundred;
  return distanceSquared(context.fighter(id).position, context.fighter(target).position) <=
         reach + band;
}

bool positionFor(BattleContext& context, FighterId id, FighterId target, const BattleMove& move,
                 bool pointBlank) {
  if (target == id || move.range == MoveRange::Self || move.range == MoveRange::None) {
    face(context, id, target);
    return true;
  }
  const Fighter& self = context.fighter(id);
  const std::int64_t distance = distanceSquared(self.position, context.fighter(target).position);
  const std::int64_t wanted =
      pointBlank ? contactRangeSquared(context, id, target) : reachOf(context, id, target, move);
  if (distance <= std::min(wanted, reachOf(context, id, target, move))) {
    face(context, id, target);
    return true;
  }
  approach(context, id, target);
  return false;
}

Command stanceCommand(Stance stance) {
  switch (stance) {
  case Stance::Aggressive:
    return Command::Attack;
  case Stance::Hold:
    return Command::Moderate;
  case Stance::Distance:
    return Command::Distance;
  case Stance::Guard:
    return Command::Defend;
  }
  return Command::Attack;
}

void folderSetStance(BattleContext& context, Stance stance, FighterId side) {
  FolderFighter& own = folderFighter(context, side);
  if (own.stance == stance) {
    return;
  }
  own.stance = stance;
  context.state->commandsOf(side).current = stanceCommand(stance);
  context.emit({.kind = EventKind::StanceChanged,
                .fighter = side,
                .other = std::nullopt,
                .move = std::nullopt,
                .amount = static_cast<std::int32_t>(stance),
                .status = StatusEffect::None,
                .command = Command::None});
}

} // namespace dwe::game::battle
