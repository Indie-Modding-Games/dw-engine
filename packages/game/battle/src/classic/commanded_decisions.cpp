#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "classic/classic.hpp"
#include "mechanics/actions/actions.hpp"
#include "mechanics/decisions/decisions.hpp"
#include "mechanics/decisions/targeting.hpp"
#include "mechanics/decisions/usability.hpp"
#include "mechanics/status/status.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;

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
        if (context.state->isEnemy(other) && self.target != other && skipped != other) {
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

} // namespace

void tickPartnerAi(BattleContext& context) {
  if (!context.state->partner().gone()) {
    tickCommanded(context, PartnerId);
  }
}

void tickEnemyAi(BattleContext& context) {
  decayBuffPriorities(context);
  BattleState& state = *context.state;
  bool stop = false;
  // A finisher's freeze stops the enemies after the one that saw it; the co-op allies after them
  // still tick, as the partner did before them.
  for (FighterId id = 1; id < state.fighters.size(); ++id) {
    if (state.fighters[id].gone()) {
      continue;
    }
    if (state.commanded(id)) {
      tickCommanded(context, id);
    } else if (!stop) {
      tickEnemy(context, id, stop);
    }
  }
}

} // namespace dwe::game::battle
