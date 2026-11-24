#include <dwe/game/battle/messages.hpp>

#include <cstdint>
#include <optional>

#include "folder/fighters.hpp"
#include "folder/folder.hpp"
#include "folder/orders/orders.hpp"

namespace dwe::game::battle {

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

void enterPhase(const BattleContext& context, FighterId id, OrderPhase phase, std::int32_t frames) {
  FolderFighter& own = folderFighter(context, id);
  own.phase = phase;
  own.phaseFrames = frames;
  own.phaseLength = frames;
  if (own.active) {
    emitOrder(context, EventKind::OrderPhaseChanged, id, *own.active,
              static_cast<std::int32_t>(phase));
  }
}

void remember(FolderFighter& own, const Order& order, OrderEnd end, OrderProblem problem) {
  own.lastOrder = order;
  own.lastEnd = end;
  own.lastProblem = problem;
  if (end != OrderEnd::Done) {
    own.notice =
        OrderNotice{.order = order, .end = end, .problem = problem, .frames = OrderNoticeFrames};
  }
}

// The active order leaves the fighter, which goes back to idle.
void endOrder(BattleContext& context, FighterId id, OrderEnd end, OrderProblem problem) {
  FolderFighter& own = folderFighter(context, id);
  if (!own.active) {
    return;
  }
  const Order order = *own.active;
  remember(own, order, end, problem);
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
  remember(own, *own.active, OrderEnd::Interrupted, OrderProblem::None);
  emitOrder(context, EventKind::OrderInterrupted, id, *own.active,
            static_cast<std::int32_t>(own.active->id));
  enterPhase(context, id, OrderPhase::Positioning, 0);
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
    remember(own, replaced, OrderEnd::Replaced, OrderProblem::ByTrainer);
    emitOrder(context, EventKind::OrderReplaced, side, replaced,
              static_cast<std::int32_t>(replaced.id));
    own.queue.back() = order;
  } else {
    own.queue.push_back(order);
  }
  emitOrder(context, EventKind::OrderReceived, side, order, static_cast<std::int32_t>(order.id));
  return {};
}

void folderCancelOrders(BattleContext& context, FighterId side) {
  FolderFighter& own = folderFighter(context, side);
  if (!own.queue.empty()) {
    remember(own, own.queue.front(), OrderEnd::Cancelled, OrderProblem::ByTrainer);
  }
  for (const Order& order : own.queue) {
    emitOrder(context, EventKind::OrderCancelled, side, order,
              static_cast<std::int32_t>(OrderProblem::ByTrainer));
  }
  own.queue.clear();
}

} // namespace dwe::game::battle
