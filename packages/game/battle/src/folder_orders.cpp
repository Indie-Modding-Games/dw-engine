#include <dwe/game/battle/folder_formulas.hpp>
#include <dwe/game/battle/messages.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

#include "folder_battle.hpp"
#include "usability.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;
// Distance keeps the fighter between this share of its preferred distance (squared) and all of it.
constexpr std::int64_t DistanceBandPercent = 80;
// Guard keeps within twice the contact reach of the ally it protects (four times squared).
constexpr std::int64_t GuardReachSquaredTimes = 4;

const FolderRules& rulesOf(const BattleContext& context) {
  return *context.rules->folder;
}

std::optional<MoveRef> moveOf(const BattleContext& context, FighterId id, std::size_t slot) {
  return slot < EquippedSlots ? context.fighter(id).moves.at(slot) : std::nullopt;
}

TechniqueTiming timingOf(const BattleContext& context, FighterId id, const Order& order) {
  if (order.slot == FinisherSlot) {
    return rulesOf(context).finisherTiming;
  }
  const std::optional<MoveRef> move = moveOf(context, id, order.slot);
  return move ? folder::timingOf(rulesOf(context), context.moves->at(*move))
              : TechniqueTiming{.preparationFrames = rulesOf(context).preparationFrames,
                                .recoveryFrames = rulesOf(context).recoveryFrames};
}

void emitOrder(const BattleContext& context, EventKind kind, FighterId id, const Order& order,
               std::int32_t amount) {
  context.emit({.kind = kind,
                .fighter = id,
                .other = order.target,
                .move = moveOf(context, id, order.slot),
                .amount = amount,
                .status = StatusEffect::None,
                .command = Command::None});
}

void enter(const BattleContext& context, FighterId id, OrderPhase phase, std::int32_t frames) {
  FolderFighter& own = folderFighter(context, id);
  own.phase = phase;
  own.phaseFrames = frames;
  own.phaseLength = frames;
  if (own.active) {
    emitOrder(context, EventKind::OrderPhaseChanged, id, *own.active,
              static_cast<std::int32_t>(phase));
  }
}

// The active order leaves the fighter, which goes back to idle.
void endOrder(BattleContext& context, FighterId id, OrderEnd end, OrderProblem problem) {
  FolderFighter& own = folderFighter(context, id);
  if (!own.active) {
    return;
  }
  const Order order = *own.active;
  own.lastOrder = order;
  own.lastEnd = end;
  own.lastProblem = problem;
  switch (end) {
  case OrderEnd::Done:
    emitOrder(context, EventKind::OrderDone, id, order, static_cast<std::int32_t>(order.id));
    break;
  case OrderEnd::Interrupted:
    emitOrder(context, EventKind::OrderInterrupted, id, order, static_cast<std::int32_t>(order.id));
    break;
  case OrderEnd::Invalid:
    emitOrder(context, EventKind::OrderInvalid, id, order, static_cast<std::int32_t>(problem));
    break;
  case OrderEnd::Cancelled:
  case OrderEnd::Replaced:
    emitOrder(context, EventKind::OrderCancelled, id, order, static_cast<std::int32_t>(problem));
    break;
  }
  own.active.reset();
  context.fighter(id).queuedMove.reset();
  own.phase = OrderPhase::Idle;
  own.phaseFrames = 0;
  own.phaseLength = 0;
}

// §2.8, §4.5: an interrupted order waits again at the front of the queue; a full queue drops its
// last order for it.
void interrupt(BattleContext& context, FighterId id) {
  FolderFighter& own = folderFighter(context, id);
  if (!own.active) {
    return;
  }
  const Order order = *own.active;
  endOrder(context, id, OrderEnd::Interrupted, OrderProblem::None);
  if (own.queue.size() >= OrderQueueSize) {
    emitOrder(context, EventKind::OrderReplaced, id, own.queue.back(),
              static_cast<std::int32_t>(own.queue.back().id));
    own.queue.pop_back();
  }
  own.queue.insert(own.queue.begin(), order);
}

// §4.5: a hit cut the order's preparation or its technique; the order stays and is retried from
// positioning once the hit reaction ends.
void setBack(BattleContext& context, FighterId id) {
  FolderFighter& own = folderFighter(context, id);
  if (!own.active) {
    return;
  }
  own.lastOrder = own.active;
  own.lastEnd = OrderEnd::Interrupted;
  own.lastProblem = OrderProblem::None;
  emitOrder(context, EventKind::OrderInterrupted, id, *own.active,
            static_cast<std::int32_t>(own.active->id));
  enter(context, id, OrderPhase::Positioning, 0);
}

bool standing(const BattleContext& context, FighterId id) {
  const Fighter& fighter = context.fighter(id);
  return !fighter.gone() && !fighter.fainted && !fighter.defeated();
}

bool validFoe(const BattleContext& context, FighterId self, std::optional<FighterId> other) {
  return other && *other < context.state->fighters.size() && context.state->foes(self, *other) &&
         standing(context, *other);
}

OrderProblem slotProblem(const BattleContext& context, FighterId id, std::size_t slot) {
  return folderSlotProblem(*context.state, *context.moves, *context.rules, context.conditionOf(id),
                           id, slot);
}

bool needsFoe(const BattleMove& move) {
  return move.range != MoveRange::Self && !move.buff;
}

// The squared distance at which `move` reaches `target` from `id`.
std::int64_t reachOf(const BattleContext& context, FighterId id, FighterId target,
                     const BattleMove& move) {
  const std::int64_t contact = contactRangeSquared(context, id, target);
  return move.range == MoveRange::Melee ? contact : move.distanceSquared + contact;
}

void face(const BattleContext& context, FighterId id, FighterId target) {
  Fighter& fighter = context.fighter(id);
  standIdle(fighter);
  if (target != id) {
    lookAt(fighter, context.fighter(target).position);
  }
}

// §2.9: where the stance keeps the fighter while it has no technique to position for.
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

// Whether `move` reaches `target` now; a ranged one keeps the Classic band around its distance.
bool inReach(const BattleContext& context, FighterId id, FighterId target, const BattleMove& move) {
  const std::int64_t reach = reachOf(context, id, target, move);
  const std::int64_t band = move.range == MoveRange::Melee
                                ? 0
                                : reach * context.rules->pacing.rangedBandPercent / Hundred;
  return distanceSquared(context.fighter(id).position, context.fighter(target).position) <=
         reach + band;
}

// §2.9, §7.4: walks until the technique reaches its target; true once it does. `pointBlank`
// (Aggressive, for `folder.chase_frames`) closes to contact first.
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

// The end of processing (§4.2 steps 2 and 3): the order's target and its validation.
void validate(BattleContext& context, FighterId id) {
  FolderFighter& own = folderFighter(context, id);
  Order& order = *own.active;
  const OrderProblem problem = slotProblem(context, id, order.slot);
  if (problem != OrderProblem::None) {
    endOrder(context, id, OrderEnd::Invalid, problem);
    return;
  }
  Fighter& self = context.fighter(id);
  const BattleMove& move = context.moves->at(*moveOf(context, id, order.slot));
  if (!needsFoe(move)) {
    order.target = id;
  } else if (!order.target) {
    order.target = validFoe(context, id, self.target) ? self.target : nearestFoe(context, id);
  }
  if (needsFoe(move) && !validFoe(context, id, order.target)) {
    endOrder(context, id, OrderEnd::Cancelled,
             order.target ? OrderProblem::TargetGone : OrderProblem::NoTarget);
    return;
  }
  if (*order.target != id) {
    setTarget(context, id, order.target);
  }
  self.queuedMove = moveOf(context, id, order.slot);
  self.outOfRangeFrames = 0;
  enter(context, id, OrderPhase::Positioning, 0);
}

// The order still stands while positioning and preparing: its target and its MP.
bool stillValid(BattleContext& context, FighterId id) {
  const Order& order = *folderFighter(context, id).active;
  if (order.target != id && !validFoe(context, id, order.target)) {
    endOrder(context, id, OrderEnd::Cancelled, OrderProblem::TargetGone);
    return false;
  }
  if (const OrderProblem problem = slotProblem(context, id, order.slot);
      problem != OrderProblem::None) {
    endOrder(context, id, OrderEnd::Invalid, problem);
    return false;
  }
  return true;
}

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
  if (own.phase != OrderPhase::Executing || self.attack || context.state->finisher.fighter == id) {
    return;
  }
  if (self.coolingDown) {
    self.coolingDown = false;
    self.cooldown = 0;
    enter(context, id, OrderPhase::Recovering,
          own.active ? timingOf(context, id, *own.active).recoveryFrames
                     : rulesOf(context).recoveryFrames);
    return;
  }
  setBack(context, id);
}

// The foe a strip order aims at: the one Change picked, kept by the order from then on.
std::optional<FighterId> aimOf(const BattleContext& context, FighterId side) {
  const std::optional<FighterId> target = context.fighter(side).target;
  return validFoe(context, side, target) ? target : std::nullopt;
}

} // namespace

OrderProblem folderSlotProblem(const BattleState& state, const MoveCatalog& moves,
                               const BattleRules& rules, const PartnerCondition& condition,
                               FighterId id, std::size_t slot) {
  const Fighter& fighter = state.fighters.at(id);
  const std::optional<MoveRef> ref = slot < EquippedSlots ? fighter.moves.at(slot) : std::nullopt;
  if (!ref) {
    return OrderProblem::EmptySlot;
  }
  const BattleMove& move = moves.at(*ref);
  if (move.finisher) {
    return fighter.finisherProgress < fighter.finisherGoal ? OrderProblem::FinisherNotReady
                                                           : OrderProblem::None;
  }
  const std::int32_t cost = partnerMpCost(rules, move.mpCost, fighter.stats.brain,
                                          condition.sick || condition.injured, MpCostUse::Check);
  return fighter.mp - fighter.pendingMp < cost ? OrderProblem::NotEnoughMp : OrderProblem::None;
}

std::optional<FighterId> nearestFoe(const BattleContext& context, FighterId id) {
  const BattleState& state = *context.state;
  const Fighter& self = state.fighters.at(id);
  std::optional<FighterId> nearest;
  std::int64_t best = 0;
  for (FighterId other = 0; other < state.fighters.size(); ++other) {
    if (!validFoe(context, id, other)) {
      continue;
    }
    const std::int64_t distance = distanceSquared(self.position, state.fighters[other].position);
    if (!nearest || distance < best) {
      nearest = other;
      best = distance;
    }
  }
  return nearest;
}

core::Result<void> folderOrder(BattleContext& context, std::size_t slot,
                               std::optional<FighterId> target, FighterId side) {
  if (slot >= EquippedSlots || !moveOf(context, side, slot)) {
    return core::fail(messages::formatMessage(messages::NoSuchSlot, slot));
  }
  if (target && (*target >= context.state->fighters.size() ||
                 (*target != side && !context.state->foes(side, *target)))) {
    return core::fail(messages::formatMessage(messages::NotATarget, *target));
  }
  FolderState& folder = *context.state->folder;
  FolderFighter& own = folder.fighters.at(side);
  const Order order{.slot = slot, .target = target, .id = folder.nextOrder++};
  if (own.queue.size() >= OrderQueueSize) {
    const Order replaced = own.queue.back();
    own.lastOrder = replaced;
    own.lastEnd = OrderEnd::Replaced;
    own.lastProblem = OrderProblem::ByTrainer;
    emitOrder(context, EventKind::OrderReplaced, side, replaced,
              static_cast<std::int32_t>(replaced.id));
    own.queue.back() = order;
  } else {
    own.queue.push_back(order);
  }
  emitOrder(context, EventKind::OrderReceived, side, order, static_cast<std::int32_t>(order.id));
  return {};
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

std::vector<Command> folderMenu(const BattleState& state, FighterId id) {
  const Fighter& fighter = state.fighters.at(id);
  std::vector<Command> menu{Command::Finisher};
  if (!state.scripted) {
    menu.push_back(Command::Flee);
  }
  for (const Command command :
       {Command::Change, Command::Defend, Command::Distance, Command::Moderate, Command::Attack}) {
    menu.push_back(command);
  }
  for (const Command technique : {Command::Technique3, Command::Technique2, Command::Technique1}) {
    const auto slot =
        static_cast<std::size_t>(technique) - static_cast<std::size_t>(Command::Technique1);
    if (fighter.moves.at(slot)) {
      menu.push_back(technique);
    }
  }
  menu.push_back(Command::YourCall);
  return menu;
}

core::Result<void> folderCommand(BattleContext& context, Command command, FighterId side) {
  switch (command) {
  case Command::Technique1:
  case Command::Technique2:
  case Command::Technique3:
    return folderOrder(
        context, static_cast<std::size_t>(command) - static_cast<std::size_t>(Command::Technique1),
        aimOf(context, side), side);
  case Command::Finisher:
    return folderOrder(context, FinisherSlot, aimOf(context, side), side);
  case Command::Attack:
    folderSetStance(context, Stance::Aggressive, side);
    return {};
  case Command::Moderate:
    folderSetStance(context, Stance::Hold, side);
    return {};
  case Command::Distance:
    folderSetStance(context, Stance::Distance, side);
    return {};
  case Command::Defend:
    folderSetStance(context, Stance::Guard, side);
    return {};
  case Command::YourCall:
    folderCancelOrders(context, side);
    return {};
  case Command::Change: {
    const std::optional<FighterId> current = context.fighter(side).target;
    std::optional<FighterId> next;
    for (FighterId step = 1; step <= context.state->fighters.size(); ++step) {
      const FighterId other = (current.value_or(0) + step) % context.state->fighters.size();
      if (validFoe(context, side, other)) {
        next = other;
        break;
      }
    }
    if (next) {
      setTarget(context, side, next);
    }
    return {};
  }
  default:
    return core::fail(std::string{messages::NotInFolder});
  }
}

void folderCancelOrders(BattleContext& context, FighterId side) {
  FolderFighter& own = folderFighter(context, side);
  for (const Order& order : own.queue) {
    emitOrder(context, EventKind::OrderCancelled, side, order,
              static_cast<std::int32_t>(OrderProblem::ByTrainer));
  }
  own.queue.clear();
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

void tickFolderCommanded(BattleContext& context, FighterId id) {
  BattleState& state = *context.state;
  Fighter& self = context.fighter(id);
  FolderFighter& own = folderFighter(context, id);
  if (state.fleeFramesOf(id)) {
    if (own.active && own.phase != OrderPhase::Executing) {
      endOrder(context, id, OrderEnd::Cancelled, OrderProblem::ByTrainer);
    }
    folderCancelOrders(context, id);
    if (!self.attack && !self.knockedBack && !self.blocking && !self.fainted) {
      walkToward(context, id, state.tamerOf(id));
    }
    return;
  }
  followExecution(context, id);
  if (self.knockedBack && own.phase == OrderPhase::Preparing) {
    setBack(context, id);
  }
  if (self.knockedBack || self.attack || self.blocking || self.fainted) {
    return;
  }
  if (self.hp == 0) {
    endOrder(context, id, OrderEnd::Cancelled, OrderProblem::None);
    faint(context, id);
    return;
  }
  if (state.remainingOthers(id).empty()) {
    standIdle(self);
    return;
  }
  if (state.frozenByFinisher()) {
    if (state.finisher.fighter == id && own.phase == OrderPhase::Executing) {
      startQueuedMove(context, id);
    }
    return;
  }
  if (holdForStatus(context, id)) {
    return;
  }
  self.planned = true;
  self.moveRange = -1;
  if (own.active && own.phase != OrderPhase::Processing) {
    self.queuedMove = moveOf(context, id, own.active->slot);
  }
  const FolderRules& rules = rulesOf(context);
  switch (own.phase) {
  case OrderPhase::Idle:
    if (!own.queue.empty()) {
      own.active = own.queue.front();
      own.queue.erase(own.queue.begin());
      enter(context, id, OrderPhase::Processing,
            folder::processingFrames(rules, rules.processingFrames, self.stats.brain));
    }
    moveByStance(context, id);
    break;
  case OrderPhase::Processing:
    moveByStance(context, id);
    if (--own.phaseFrames <= 0) {
      validate(context, id);
    }
    break;
  case OrderPhase::Positioning: {
    if (!stillValid(context, id)) {
      moveByStance(context, id);
      break;
    }
    const BattleMove& move = context.moves->at(*self.queuedMove);
    const bool pointBlank =
        own.effectiveStance() == Stance::Aggressive && own.phaseFrames < rules.chaseFrames;
    if (positionFor(context, id, *own.active->target, move, pointBlank)) {
      enter(context, id, OrderPhase::Preparing,
            folder::executionFrames(rules, folder::timingOf(rules, move).preparationFrames,
                                    folderStats(context, id).speed));
    } else if (++own.phaseFrames > rules.giveUpFrames) {
      endOrder(context, id, OrderEnd::Invalid, OrderProblem::Unreachable);
    }
    break;
  }
  case OrderPhase::Preparing:
    if (!stillValid(context, id)) {
      moveByStance(context, id);
      break;
    }
    face(context, id, *own.active->target);
    if (--own.phaseFrames > 0) {
      break;
    }
    if (!inReach(context, id, *own.active->target, context.moves->at(*self.queuedMove))) {
      enter(context, id, OrderPhase::Positioning, 0);
      break;
    }
    startQueuedMove(context, id);
    if (self.attack || state.finisher.fighter == id) {
      enter(context, id, OrderPhase::Executing, 0);
    }
    break;
  case OrderPhase::Executing:
    break;
  case OrderPhase::Recovering:
    moveByStance(context, id);
    if (--own.phaseFrames <= 0) {
      endOrder(context, id, OrderEnd::Done, OrderProblem::None);
    }
    break;
  }
}

} // namespace dwe::game::battle
