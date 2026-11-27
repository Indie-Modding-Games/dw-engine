#include <dwe/game/battle/folder/formulas.hpp>

#include <cstdint>
#include <utility>

#include "folder/fighters.hpp"
#include "folder/folder.hpp"
#include "folder/orders/orders.hpp"
#include "folder/stances/stances.hpp"
#include "mechanics/actions/actions.hpp"
#include "mechanics/decisions/decisions.hpp"
#include "mechanics/movement/movement.hpp"

namespace dwe::game::battle {

namespace {

// One commanded fighter on one frame, as each phase of its order sees it.
struct OrderTurn {
  BattleContext& context;
  FighterId id;
  Fighter& self;
  FolderFighter& own;
  const FolderRules& rules;
};

// A status effect holds the fighter: the original's behaviour for it runs (stunned stands, confused
// strikes at random, flattened shoots), and the order waits again.
bool holdForStatus(BattleContext& context, FighterId id) {
  Fighter& self = context.fighter(id);
  FolderFighter& own = folderFighter(context, id);
  const StatusState& status = self.status;
  const bool held = status.stunned || status.confused || status.flattened || status.flatFrames != 0;
  if (!held) {
    if (own.held) {
      own.held = false;
      self.queuedMove.reset();
      self.chargingUp = false;
      self.coolingDown = false;
      self.cooldown = 0;
      self.planned = true;
      self.moveRange = -1;
    }
    return false;
  }
  if (!own.held) {
    own.held = true;
    if (own.phase != OrderPhase::Idle && own.phase != OrderPhase::Recovering) {
      interrupt(context, id);
    }
    self.planned = false;
    self.moveRange = static_cast<std::int32_t>(MoveRange::None);
    self.queuedMove.reset();
  }
  if (status.flattened && !self.planned) {
    self.queuedMove = context.moves->flattenedShot();
    selectTarget(context, id, self.target, false);
    self.moveRange = static_cast<std::int32_t>(MoveRange::Ranged);
    self.planned = true;
  } else if (status.confused && !self.planned && !self.chargingUp && !self.coolingDown) {
    chooseConfusedMove(context, id);
  }
  return true;
}

// The technique of the order ended (recovery follows) or a hit cut it (it waits again).
void followExecution(BattleContext& context, FighterId id) {
  Fighter& self = context.fighter(id);
  FolderFighter& own = folderFighter(context, id);
  const bool committed = std::exchange(own.committed, false);
  if (own.phase != OrderPhase::Executing || self.attack || context.state->finisher.fighter == id) {
    return;
  }
  if (self.coolingDown || committed) {
    self.coolingDown = false;
    self.cooldown = 0;
    enterPhase(context, id, OrderPhase::Recovering,
               own.active ? timingOf(context, id, *own.active).recoveryFrames
                          : folderRules(context).recoveryFrames);
    return;
  }
  setBack(context, id);
}

// §4.2 step 1: the next order leaves the queue for processing; meanwhile the stance places the
// fighter.
void tickIdle(const OrderTurn& turn) {
  if (!turn.own.queue.empty()) {
    turn.own.active = turn.own.queue.front();
    turn.own.queue.erase(turn.own.queue.begin());
    enterPhase(
        turn.context, turn.id, OrderPhase::Processing,
        folder::processingFrames(turn.rules, turn.rules.processingFrames, turn.self.stats.brain));
  }
  moveByStance(turn.context, turn.id);
}

void tickProcessing(const OrderTurn& turn) {
  moveByStance(turn.context, turn.id);
  if (--turn.own.phaseFrames <= 0) {
    validate(turn.context, turn.id);
  }
}

void tickPositioning(const OrderTurn& turn) {
  if (!stillValid(turn.context, turn.id)) {
    moveByStance(turn.context, turn.id);
    return;
  }
  const BattleMove& move = turn.context.moves->at(*turn.self.queuedMove);
  const bool pointBlank = turn.own.effectiveStance() == Stance::Aggressive &&
                          turn.own.phaseFrames < turn.rules.chaseFrames;
  if (positionFor(turn.context, turn.id, *turn.own.active->target, move, pointBlank)) {
    enterPhase(turn.context, turn.id, OrderPhase::Preparing,
               folder::executionFrames(turn.rules,
                                       folder::timingOf(turn.rules, move).preparationFrames,
                                       folderStats(turn.context, turn.id).speed));
  } else if (++turn.own.phaseFrames > turn.rules.giveUpFrames) {
    endOrder(turn.context, turn.id, OrderEnd::Invalid, OrderProblem::Unreachable);
  }
}

void tickPreparing(const OrderTurn& turn) {
  if (!stillValid(turn.context, turn.id)) {
    moveByStance(turn.context, turn.id);
    return;
  }
  face(turn.context, turn.id, *turn.own.active->target);
  if (--turn.own.phaseFrames > 0) {
    return;
  }
  if (!inReach(turn.context, turn.id, *turn.own.active->target,
               turn.context.moves->at(*turn.self.queuedMove))) {
    enterPhase(turn.context, turn.id, OrderPhase::Positioning, 0);
    return;
  }
  startQueuedMove(turn.context, turn.id);
  if (turn.self.attack || turn.context.state->finisher.fighter == turn.id) {
    enterPhase(turn.context, turn.id, OrderPhase::Executing, 0);
  }
}

void tickRecovering(const OrderTurn& turn) {
  moveByStance(turn.context, turn.id);
  if (--turn.own.phaseFrames <= 0) {
    endOrder(turn.context, turn.id, OrderEnd::Done, OrderProblem::None);
  }
}

// The trainer called the fighter back: the orders drop and it runs back to them.
void flee(BattleContext& context, FighterId id) {
  Fighter& self = context.fighter(id);
  FolderFighter& own = folderFighter(context, id);
  if (own.active && own.phase != OrderPhase::Executing) {
    endOrder(context, id, OrderEnd::Cancelled, OrderProblem::ByTrainer);
  }
  folderCancelOrders(context, id);
  if (!self.attack && !self.knockedBack && !self.blocking && !self.fainted) {
    walkToward(context, id, context.state->tamerOf(id));
  }
}

// What keeps the order from going on this frame: a reaction, a faint, the end of the foes, a
// finisher's freeze or a status. True when one did.
bool heldThisFrame(BattleContext& context, FighterId id) {
  BattleState& state = *context.state;
  Fighter& self = context.fighter(id);
  FolderFighter& own = folderFighter(context, id);
  followExecution(context, id);
  if (self.knockedBack && own.phase == OrderPhase::Preparing) {
    setBack(context, id);
  }
  if (self.knockedBack || self.attack || self.blocking || self.fainted) {
    return true;
  }
  if (self.hp == 0) {
    endOrder(context, id, OrderEnd::Cancelled, OrderProblem::None);
    faint(context, id);
    return true;
  }
  if (state.remainingOthers(id).empty()) {
    standIdle(self);
    return true;
  }
  if (state.frozenByFinisher()) {
    if (state.finisher.fighter == id && own.phase == OrderPhase::Executing) {
      startQueuedMove(context, id);
    }
    return true;
  }
  return holdForStatus(context, id);
}

} // namespace

void tickFolderCommanded(BattleContext& context, FighterId id) {
  if (context.state->fleeFramesOf(id)) {
    flee(context, id);
    return;
  }
  if (heldThisFrame(context, id)) {
    return;
  }
  const OrderTurn turn{.context = context,
                       .id = id,
                       .self = context.fighter(id),
                       .own = folderFighter(context, id),
                       .rules = folderRules(context)};
  turn.self.planned = true;
  turn.self.moveRange = -1;
  if (turn.own.active && turn.own.phase != OrderPhase::Processing) {
    turn.self.queuedMove = moveOf(context, id, turn.own.active->slot);
  }
  switch (turn.own.phase) {
  case OrderPhase::Idle:
    tickIdle(turn);
    break;
  case OrderPhase::Processing:
    tickProcessing(turn);
    break;
  case OrderPhase::Positioning:
    tickPositioning(turn);
    break;
  case OrderPhase::Preparing:
    tickPreparing(turn);
    break;
  case OrderPhase::Executing:
    break;
  case OrderPhase::Recovering:
    tickRecovering(turn);
    break;
  }
}

} // namespace dwe::game::battle
