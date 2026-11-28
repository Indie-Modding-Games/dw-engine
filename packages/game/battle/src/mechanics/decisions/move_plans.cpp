#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "mechanics/decisions/decisions.hpp"
#include "mechanics/decisions/usability.hpp"

namespace dwe::game::battle {

void resetAction(Fighter& fighter) {
  fighter.cooldown = 0;
  fighter.disobeyFrames = 0;
  fighter.chargingUp = false;
  fighter.coolingDown = false;
  fighter.disobeying = false;
}

void applyChargeRequirement(BattleContext& context, FighterId id, MoveRef move) {
  Fighter& fighter = context.fighter(id);
  const std::int32_t cap = context.rules->speedReserve.cap;
  switch (fighter.chargeMode) {
  case ChargeMode::Aggressive:
    fighter.chargingUp = fighter.chargingUp || fighter.speedReserve <= 0;
    break;
  case ChargeMode::Normal:
    fighter.chargingUp =
        fighter.chargingUp ||
        (fighter.speedReserve != cap && fighter.speedReserve < context.moves->at(move).power);
    break;
  case ChargeMode::Cautious:
    fighter.chargingUp = fighter.chargingUp || fighter.speedReserve < cap;
    break;
  }
}

void queueMove(BattleContext& context, FighterId id, std::size_t slot) {
  Fighter& fighter = context.fighter(id);
  fighter.outOfRangeFrames = 0;
  fighter.queuedMove = fighter.moves.at(slot);
  if (!fighter.queuedMove) {
    return;
  }
  fighter.moveRange = static_cast<std::int32_t>(context.moves->at(*fighter.queuedMove).range);
  applyChargeRequirement(context, id, *fighter.queuedMove);
  fighter.planned = true;
  context.emit({.kind = EventKind::MoveQueued,
                .fighter = id,
                .other = fighter.target,
                .move = fighter.queuedMove,
                .amount = 0,
                .status = StatusEffect::None,
                .command = Command::None});
}

void waitWithoutMove(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  fighter.cooldown = context.rules->pacing.noMoveFrames;
  fighter.chargingUp = true;
}

void chooseConfusedMove(BattleContext& context, FighterId id) {
  if (context.random->chance(context.rules->partnerAi.confusedAirPercent)) {
    setTarget(context, id, std::nullopt);
  } else {
    const std::vector<FighterId> others = context.state->remainingOthers(id);
    if (!others.empty()) {
      setTarget(context, id, others.at(randomIndex(*context.random, others.size())));
    }
  }
  const std::optional<std::size_t> slot =
      randomUsableSlot(*context.random, usableSlots(context.view(), id));
  if (!slot) {
    waitWithoutMove(context, id);
    return;
  }
  queueMove(context, id, *slot);
}

} // namespace dwe::game::battle
