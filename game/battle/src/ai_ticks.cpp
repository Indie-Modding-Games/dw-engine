#include <cstddef>
#include <optional>
#include <vector>

#include "battle_context.hpp"
#include "targeting.hpp"
#include "usability.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;
// A wait ends when its counter drops under this (battle_main.c: cooldown < 2).
constexpr std::int32_t WaitEndsBelow = 2;

std::size_t randomIndex(ruleset::Random& random, std::size_t count) {
  return static_cast<std::size_t>(random.below(static_cast<std::int32_t>(count)));
}

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

void resetFlatten(Fighter& fighter) {
  fighter.status.flattened = false;
  fighter.status.flatFrames = 0;
}

void clearStatusFlags(Fighter& fighter) {
  fighter.status.poisoned = false;
  fighter.status.confused = false;
  fighter.status.stunned = false;
  fighter.status.flattened = false;
}

// battle_main.c: BTL_tickPartnerAI, the change command: the next enemy in turn.
void changeTarget(BattleContext& context, const std::vector<FighterId>& enemies) {
  Fighter& partner = context.state->partner();
  CommandState& commands = context.state->commands;
  if (enemies.size() == 2) {
    setTarget(context, PartnerId, partner.target == enemies[0] ? enemies[1] : enemies[0]);
  } else if (enemies.size() == 3) {
    auto& history = commands.changeHistory;
    if (!history[0] && !history[1]) {
      setTarget(context, PartnerId, nearestOther(*context.state, PartnerId));
      history.at(commands.changeTurn) = partner.target;
      commands.changeTurn ^= 1U;
    } else {
      const std::optional<FighterId> skipped = history.at(commands.changeTurn ^ 1U);
      std::optional<FighterId> next;
      for (FighterId id = 1; id < context.state->fighters.size(); ++id) {
        if (partner.target != id && skipped != id) {
          next = id;
          break;
        }
      }
      history.at(commands.changeTurn) = partner.target;
      commands.changeTurn ^= 1U;
      if (next) {
        setTarget(context, PartnerId, next);
      }
    }
  }
  commands.changeTarget = false;
}

// The commands that act on their own and end the partner's decisions for the frame; false when the
// partner goes on to decide by itself.
bool followCommand(BattleContext& context, const std::vector<FighterId>& enemies) {
  Fighter& partner = context.state->partner();
  CommandState& commands = context.state->commands;
  switch (commands.current) {
  case Command::Change:
    if (!partner.target || !commands.changeTarget) {
      break;
    }
    changeTarget(context, enemies);
    return true;
  case Command::Technique1:
  case Command::Technique2:
  case Command::Technique3: {
    const auto slot =
        static_cast<std::size_t>(commands.current) - static_cast<std::size_t>(Command::Technique1);
    if (!moveUsable(context.view(), PartnerId, slot)) {
      break;
    }
    if (partner.target && context.fighter(*partner.target).defeated()) {
      selectTarget(context, PartnerId, partner.target, false);
    }
    if (partner.queuedMove != partner.moves.at(slot) || partner.moveRange <= 0) {
      queueMove(context, PartnerId, slot);
    }
    if (const std::optional<MoveRef> move = partner.moves.at(slot)) {
      applyChargeRequirement(context, PartnerId, *move);
    }
    return true;
  }
  case Command::Finisher:
    if (partner.target && context.fighter(*partner.target).defeated()) {
      selectTarget(context, PartnerId, partner.target, false);
    }
    if (partner.hasFinisher() &&
        (partner.queuedMove != partner.moves.at(FinisherSlot) || partner.moveRange <= 0)) {
      queueMove(context, PartnerId, FinisherSlot);
    }
    return true;
  default:
    break;
  }
  if (commands.current != Command::Attack && commands.current != Command::Moderate) {
    partner.chargeMode = commands.savedChargeMode;
  }
  return false;
}

// The partner picks its move by the command: the strongest for attack, the cheapest for moderate,
// else its own call.
void decidePartnerMove(BattleContext& context) {
  Fighter& partner = context.state->partner();
  const Command current = context.state->commands.current;
  const bool attack = current == Command::Attack;
  const bool moderate = current == Command::Moderate;
  const std::array<bool, EquippedSlots> usable = usableSlots(context.view(), PartnerId);
  if ((attack || moderate) && !anyUsable(usable)) {
    waitWithoutMove(context, PartnerId);
    return;
  }
  MoveStyle style = MoveStyle::YourCall;
  if (attack) {
    style = MoveStyle::Strongest;
  } else if (moderate) {
    style = MoveStyle::Cheapest;
  } else if (!anyUsable(usable)) {
    waitWithoutMove(context, PartnerId);
    return;
  }
  const MoveDecision decision =
      context.strategies->partnerAi->chooseMove(context.view(), style, usable);
  if (attack) {
    partner.chargeMode = ChargeMode::Aggressive;
  } else if (moderate) {
    partner.chargeMode = ChargeMode::Cautious;
  }
  if (decision.slot) {
    queueMove(context, PartnerId, *decision.slot);
  } else {
    waitWithoutMove(context, PartnerId);
  }
}

void tickEnemy(BattleContext& context, FighterId id, bool& stop) {
  Fighter& enemy = context.fighter(id);
  if (!context.state->frozenByFinisher()) {
    tickTimers(context, id, 0);
  }
  if (enemy.knockedBack || enemy.attack || enemy.blocking || enemy.fainted) {
    return;
  }
  if (enemy.hp == 0) {
    faint(context, id);
    return;
  }
  if (enemy.defeated()) {
    standIdle(enemy);
    enemy.moveRange = -1;
    resetFlatten(enemy);
    removeStatusVisuals(context, id);
    clearStatusFlags(enemy);
    return;
  }
  if (context.state->partner().hp == 0) {
    standIdle(enemy);
    resetFlatten(enemy);
    enemy.knockedBack = false;
    enemy.blocking = false;
    enemy.planned = true;
    enemy.moveRange = -1;
    return;
  }
  if (context.state->frozenByFinisher()) {
    stop = true;
    return;
  }
  if (enemy.planned) {
    return;
  }
  if (enemy.status.flattened) {
    enemy.queuedMove = context.moves->flattenedShot();
    setTarget(context, id, PartnerId);
    enemy.moveRange = static_cast<std::int32_t>(MoveRange::Ranged);
    enemy.planned = true;
    return;
  }
  if (enemy.status.stunned) {
    return;
  }
  if (enemy.status.confused) {
    chooseConfusedMove(context, id);
    return;
  }
  if (enemy.disobeying || enemy.chargingUp || enemy.coolingDown) {
    return;
  }
  if (enemy.status.flatFrames == 0 && enemy.hasFinisher() &&
      enemy.finisherProgress == enemy.finisherGoal) {
    setTarget(context, id, PartnerId);
    queueMove(context, id, FinisherSlot);
    enemy.finisherProgress = 0;
    return;
  }
  if (!enemy.chasingAttacker) {
    setTarget(context, id, PartnerId);
  }
  if (context.state->matchClock) {
    // std_main.c: STD_tickEnemyAI: a tournament rival is cautious one time in ten, otherwise it
    // rolls aggressive or normal.
    enemy.chargeMode = context.random->below(context.rules->enemyAi.matchCautiousRoll) == 0
                           ? ChargeMode::Cautious
                           : static_cast<ChargeMode>(context.random->below(2));
  } else {
    // The original makes an enemy off the screen cautious; the engine has no screen here (Hueco),
    // so it rolls.
    enemy.chargeMode = static_cast<ChargeMode>(context.random->below(2));
  }
  const std::array<bool, EquippedSlots> usable = usableSlots(context.view(), id);
  if (!anyUsable(usable)) {
    enemy.chargeMode = ChargeMode::Cautious;
    waitWithoutMove(context, id);
    return;
  }
  const MoveDecision decision = context.strategies->enemyAi->chooseMove(context.view(), id, usable);
  if (decision.slot) {
    queueMove(context, id, *decision.slot);
  } else {
    waitWithoutMove(context, id);
  }
}

} // namespace

void setTarget(BattleContext& context, FighterId id, std::optional<FighterId> target) {
  Fighter& fighter = context.fighter(id);
  if (fighter.target != target && target) {
    context.emit({.kind = EventKind::TargetChanged,
                  .fighter = id,
                  .other = target,
                  .move = std::nullopt,
                  .amount = 0,
                  .status = StatusEffect::None,
                  .command = Command::None});
  }
  fighter.target = target;
}

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
  const std::array<bool, EquippedSlots> usable = usableSlots(context.view(), id);
  std::vector<std::size_t> slots;
  for (std::size_t slot = 0; slot < EquippedSlots; ++slot) {
    if (usable.at(slot)) {
      slots.push_back(slot);
    }
  }
  if (slots.empty()) {
    waitWithoutMove(context, id);
    return;
  }
  queueMove(context, id, slots.at(randomIndex(*context.random, slots.size())));
}

void selectTarget(BattleContext& context, FighterId id, std::optional<FighterId> toward,
                  bool forced) {
  Fighter& fighter = context.fighter(id);
  if (id != PartnerId || forced) {
    if (!toward) {
      setTarget(context, id, PartnerId);
      return;
    }
    if (id != PartnerId &&
        !context.strategies->obedience->enemyTurnsToAttacker(context.view(), id)) {
      setTarget(context, id, PartnerId);
      return;
    }
    if (context.fighter(*toward).defeated()) {
      return;
    }
    fighter.chasingAttacker = true;
    setTarget(context, id, toward);
    return;
  }
  setTarget(context, id, context.strategies->partnerAi->chooseTarget(context.view()));
}

void tickPartnerAi(BattleContext& context) {
  BattleState& state = *context.state;
  Fighter& partner = state.partner();
  CommandState& commands = state.commands;
  if (commands.delay == 0) {
    if (commands.current != commands.buffered) {
      context.emit({.kind = EventKind::CommandGiven,
                    .fighter = PartnerId,
                    .other = std::nullopt,
                    .move = std::nullopt,
                    .amount = 0,
                    .status = StatusEffect::None,
                    .command = commands.buffered});
    }
    commands.current = commands.buffered;
  } else if (!isHeld(partner) && partner.status.flatFrames == 0) {
    --commands.delay;
  }
  if (!state.frozenByFinisher()) {
    tickTimers(context, PartnerId, context.condition->discipline);
  }
  if (partner.knockedBack || partner.attack || partner.blocking || partner.fainted) {
    return;
  }
  if (partner.hp == 0) {
    faint(context, PartnerId);
    return;
  }
  const std::vector<FighterId> enemies = state.remainingOthers(PartnerId);
  if (enemies.empty()) {
    standIdle(partner);
    partner.moveRange = -1;
    resetFlatten(partner);
    removeStatusVisuals(context, PartnerId);
    clearStatusFlags(partner);
    resetAction(partner);
    partner.chasingAttacker = false;
    partner.planned = true;
    return;
  }
  if (state.frozenByFinisher()) {
    return;
  }
  if (!isHeld(partner) && partner.status.flatFrames == 0 && followCommand(context, enemies)) {
    return;
  }
  if (partner.status.flattened && state.frame % Hundred == 0) {
    setTarget(context, PartnerId, enemies.at(randomIndex(*context.random, enemies.size())));
  }
  if (partner.planned) {
    return;
  }
  if (commands.current == Command::Flee) {
    partner.moveRange = static_cast<std::int32_t>(MoveRange::Melee);
    partner.planned = true;
  }
  if (partner.status.flattened) {
    partner.queuedMove = context.moves->flattenedShot();
    selectTarget(context, PartnerId, partner.target, false);
    partner.moveRange = static_cast<std::int32_t>(MoveRange::Ranged);
    partner.planned = true;
    return;
  }
  if (partner.status.stunned) {
    return;
  }
  if (partner.status.confused) {
    chooseConfusedMove(context, PartnerId);
    return;
  }
  if (partner.chargingUp || partner.coolingDown || partner.disobeying) {
    return;
  }
  if (!partner.chasingAttacker) {
    selectTarget(context, PartnerId, partner.target, false);
  }
  decidePartnerMove(context);
}

void tickEnemyAi(BattleContext& context) {
  BattleState& state = *context.state;
  if (state.frame % context.rules->partnerAi.buffPriorityDecayFrames == 0) {
    for (Fighter& fighter : state.fighters) {
      if (fighter.buffPriority != 0) {
        --fighter.buffPriority;
      }
    }
  }
  bool stop = false;
  for (FighterId id = 1; id < state.fighters.size() && !stop; ++id) {
    tickEnemy(context, id, stop);
  }
}

} // namespace dwe::game::battle
