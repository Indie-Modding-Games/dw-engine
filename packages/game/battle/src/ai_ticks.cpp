#include <cstddef>
#include <cstdint>
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

// BTL_tickEnemyAI projects an enemy's node 1 and tests it with isOffScreen(out, 0xc8, 0xa0): off
// when more than half of 200 across or 160 down from the screen's centre.
constexpr std::uint32_t EnemyBodyNode = 1;
constexpr std::int32_t OffScreenHalfWidth = 0xc8 / 2;
constexpr std::int32_t OffScreenHalfHeight = 0xa0 / 2;

// Unknown (no ground, or it does not draw the fighter): on screen, so the enemy rolls.
bool enemyOffScreen(const BattleContext& context, FighterId id) {
  if (context.ground == nullptr) {
    return false;
  }
  const std::optional<ScreenPoint> body = context.ground->fighterNodeOnScreen(id, EnemyBodyNode);
  return body && (body->x < -OffScreenHalfWidth || body->x > OffScreenHalfWidth ||
                  body->y < -OffScreenHalfHeight || body->y > OffScreenHalfHeight);
}

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

void clearStatusFlags(Fighter& fighter) {
  fighter.status.poisoned = false;
  fighter.status.confused = false;
  fighter.status.stunned = false;
  fighter.status.flattened = false;
}

// battle_main.c: BTL_tickPartnerAI, the change command: the next enemy in turn.
void changeTarget(BattleContext& context, FighterId id, const std::vector<FighterId>& enemies) {
  Fighter& self = context.fighter(id);
  CommandState& commands = context.state->commandsOf(id);
  if (enemies.size() == 2) {
    setTarget(context, id, self.target == enemies[0] ? enemies[1] : enemies[0]);
  } else if (enemies.size() == 3) {
    auto& history = commands.changeHistory;
    if (!history[0] && !history[1]) {
      setTarget(context, id, nearestOther(*context.state, id));
      history.at(commands.changeTurn) = self.target;
      commands.changeTurn ^= 1U;
    } else {
      const std::optional<FighterId> skipped = history.at(commands.changeTurn ^ 1U);
      std::optional<FighterId> next;
      for (FighterId other = 1; other < context.state->fighters.size(); ++other) {
        if (self.target != other && skipped != other) {
          next = other;
          break;
        }
      }
      history.at(commands.changeTurn) = self.target;
      commands.changeTurn ^= 1U;
      if (next) {
        setTarget(context, id, next);
      }
    }
  }
  commands.changeTarget = false;
}

// The commands that act on their own and end a commanded fighter's decisions for the frame; false
// when it goes on to decide by itself.
bool followCommand(BattleContext& context, FighterId id, const std::vector<FighterId>& enemies) {
  Fighter& self = context.fighter(id);
  CommandState& commands = context.state->commandsOf(id);
  switch (commands.current) {
  case Command::Change:
    if (!self.target || !commands.changeTarget) {
      break;
    }
    changeTarget(context, id, enemies);
    return true;
  case Command::Technique1:
  case Command::Technique2:
  case Command::Technique3: {
    const auto slot =
        static_cast<std::size_t>(commands.current) - static_cast<std::size_t>(Command::Technique1);
    if (!moveUsable(context.view(id), id, slot)) {
      break;
    }
    if (self.target && context.fighter(*self.target).defeated()) {
      selectTarget(context, id, self.target, false);
    }
    if (self.queuedMove != self.moves.at(slot) || self.moveRange <= 0) {
      queueMove(context, id, slot);
    }
    if (const std::optional<MoveRef> move = self.moves.at(slot)) {
      applyChargeRequirement(context, id, *move);
    }
    return true;
  }
  case Command::Finisher:
    if (self.target && context.fighter(*self.target).defeated()) {
      selectTarget(context, id, self.target, false);
    }
    if (self.hasFinisher() &&
        (self.queuedMove != self.moves.at(FinisherSlot) || self.moveRange <= 0)) {
      queueMove(context, id, FinisherSlot);
    }
    return true;
  default:
    break;
  }
  if (commands.current != Command::Attack && commands.current != Command::Moderate) {
    self.chargeMode = commands.savedChargeMode;
  }
  return false;
}

// A commanded fighter picks its move by the command: the strongest for attack, the cheapest for
// moderate, else its own call.
void decideCommandedMove(BattleContext& context, FighterId id) {
  Fighter& self = context.fighter(id);
  const Command current = context.state->commandsOf(id).current;
  const bool attack = current == Command::Attack;
  const bool moderate = current == Command::Moderate;
  const BattleView view = context.view(id);
  const std::array<bool, EquippedSlots> usable = usableSlots(view, id);
  if ((attack || moderate) && !anyUsable(usable)) {
    waitWithoutMove(context, id);
    return;
  }
  MoveStyle style = MoveStyle::YourCall;
  if (attack) {
    style = MoveStyle::Strongest;
  } else if (moderate) {
    style = MoveStyle::Cheapest;
  } else if (!anyUsable(usable)) {
    waitWithoutMove(context, id);
    return;
  }
  const MoveDecision decision = context.strategies->partnerAi->chooseMove(view, style, usable);
  if (attack) {
    self.chargeMode = ChargeMode::Aggressive;
  } else if (moderate) {
    self.chargeMode = ChargeMode::Cautious;
  }
  if (decision.slot) {
    queueMove(context, id, *decision.slot);
  } else {
    waitWithoutMove(context, id);
  }
}

// battle_main.c: BTL_tickPartnerAI for the partner; vs_combat.c: VS__tickDigimonAI for either side
// of a versus match.
void tickCommanded(BattleContext& context, FighterId id) {
  BattleState& state = *context.state;
  Fighter& self = context.fighter(id);
  CommandState& commands = state.commandsOf(id);
  if (commands.delay == 0) {
    if (commands.current != commands.buffered) {
      context.emit({.kind = EventKind::CommandGiven,
                    .fighter = id,
                    .other = std::nullopt,
                    .move = std::nullopt,
                    .amount = 0,
                    .status = StatusEffect::None,
                    .command = commands.buffered});
    }
    commands.current = commands.buffered;
  } else if (!isHeld(self) && self.status.flatFrames == 0) {
    --commands.delay;
  }
  if (!state.frozenByFinisher()) {
    tickTimers(context, id, context.disciplineOf(id));
  }
  if (self.knockedBack || self.attack || self.blocking || self.fainted) {
    return;
  }
  if (self.hp == 0) {
    faint(context, id);
    return;
  }
  const std::vector<FighterId> enemies = state.remainingOthers(id);
  // vs_combat.c: VS__tickDigimonAI: in a versus match a side about to fall stands idle too.
  if (enemies.empty() || (state.versus() && self.defeated())) {
    standIdle(self);
    self.moveRange = -1;
    resetFlatten(self);
    removeStatusVisuals(context, id);
    clearStatusFlags(self);
    resetAction(self);
    self.chasingAttacker = false;
    self.planned = true;
    return;
  }
  if (state.frozenByFinisher()) {
    return;
  }
  if (!isHeld(self) && self.status.flatFrames == 0 && followCommand(context, id, enemies)) {
    return;
  }
  if (self.status.flattened && state.frame % Hundred == 0) {
    setTarget(context, id, enemies.at(randomIndex(*context.random, enemies.size())));
  }
  if (self.planned) {
    return;
  }
  if (commands.current == Command::Flee) {
    self.moveRange = static_cast<std::int32_t>(MoveRange::Melee);
    self.planned = true;
  }
  if (self.status.flattened) {
    self.queuedMove = context.moves->flattenedShot();
    selectTarget(context, id, self.target, false);
    self.moveRange = static_cast<std::int32_t>(MoveRange::Ranged);
    self.planned = true;
    return;
  }
  if (self.status.stunned) {
    return;
  }
  if (self.status.confused) {
    chooseConfusedMove(context, id);
    return;
  }
  if (self.chargingUp || self.coolingDown || self.disobeying) {
    return;
  }
  if (!self.chasingAttacker) {
    selectTarget(context, id, self.target, false);
  }
  decideCommandedMove(context, id);
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
  } else if (enemyOffScreen(context, id)) {
    // battle_main.c:631-640: an enemy whose body (node 1) is off a 200 x 160 box in the middle of
    // the screen (map.c: isOffScreen) is cautious, with no roll.
    enemy.chargeMode = ChargeMode::Cautious;
  } else {
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

void resetFlatten(Fighter& fighter) {
  fighter.status.flattened = false;
  fighter.status.flatFrames = 0;
}

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
  const std::optional<std::size_t> slot =
      randomUsableSlot(*context.random, usableSlots(context.view(), id));
  if (!slot) {
    waitWithoutMove(context, id);
    return;
  }
  queueMove(context, id, *slot);
}

void selectTarget(BattleContext& context, FighterId id, std::optional<FighterId> toward,
                  bool forced) {
  // vs_combat.c: VS__tickDigimonAI: a versus side always targets the other one.
  if (context.state->versus()) {
    setTarget(context, id, id == PartnerId ? RivalId : PartnerId);
    return;
  }
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
  tickCommanded(context, PartnerId);
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
    if (state.commanded(id)) {
      tickCommanded(context, id);
    } else {
      tickEnemy(context, id, stop);
    }
  }
}

} // namespace dwe::game::battle
