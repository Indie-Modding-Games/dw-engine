#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "mechanics/decisions/decisions.hpp"

namespace dwe::game::battle {

namespace {

// A wait ends when its counter drops under this (battle_main.c: cooldown < 2).
constexpr std::int32_t WaitEndsBelow = 2;

// battle_main.c: BTL_increaseSpeedBuffer.
void growSpeedReserve(const BattleContext& context, Fighter& fighter) {
  const SpeedReserveRules& rules = context.rules->speedReserve;
  if (fighter.speedReserve >= rules.cap) {
    return;
  }
  if (context.state->frame % rules.gainEveryFrames == 0) {
    fighter.speedReserve += (fighter.stats.speed / rules.gainPerSpeed) + rules.gainBase;
  }
  fighter.speedReserve = std::min(fighter.speedReserve, rules.cap);
}

} // namespace

std::size_t randomIndex(ruleset::Random& random, std::size_t count) {
  return static_cast<std::size_t>(random.below(static_cast<std::int32_t>(count)));
}

// The obedience roll, the cooldown and the speed reserve of a frame, which a finisher's freeze
// stops.
void tickTimers(BattleContext& context, FighterId id, std::int32_t discipline) {
  Fighter& fighter = context.fighter(id);
  if (context.strategies->obedience->disobeys(context.view(), id, discipline)) {
    fighter.disobeying = true;
    fighter.disobeyFrames = context.rules->obedience.idleFrames;
    context.emit({.kind = EventKind::Disobeyed,
                  .fighter = id,
                  .other = std::nullopt,
                  .move = std::nullopt,
                  .amount = 0,
                  .status = StatusEffect::None,
                  .command = Command::None});
  }
  if (fighter.cooldown >= WaitEndsBelow) {
    --fighter.cooldown;
  }
  if (!fighter.disobeying) {
    growSpeedReserve(context, fighter);
  }
}

bool isHeld(const Fighter& fighter) {
  return fighter.status.confused || fighter.status.stunned || fighter.status.flattened ||
         fighter.fainted;
}

void clearStatusFlags(Fighter& fighter) {
  fighter.status.poisoned = false;
  fighter.status.confused = false;
  fighter.status.stunned = false;
  fighter.status.flattened = false;
}

} // namespace dwe::game::battle
