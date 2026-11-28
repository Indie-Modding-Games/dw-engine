#include <dwe/game/battle/folder/formulas.hpp>

#include <cstdint>
#include <optional>

#include "folder/fighters.hpp"
#include "folder/folder.hpp"
#include "folder/orders/orders.hpp"
#include "mechanics/decisions/decisions.hpp"

namespace dwe::game::battle {

namespace {

OrderProblem slotProblem(const BattleContext& context, FighterId id, std::size_t slot) {
  return folderSlotProblem(*context.state, *context.moves, *context.rules, context.conditionOf(id),
                           id, slot);
}

bool needsFoe(const BattleMove& move) {
  return move.range != MoveRange::Self && !move.buff;
}

} // namespace

std::optional<MoveRef> moveOf(const BattleContext& context, FighterId id, std::size_t slot) {
  return slot < EquippedSlots ? context.fighter(id).moves.at(slot) : std::nullopt;
}

TechniqueTiming timingOf(const BattleContext& context, FighterId id, const Order& order) {
  if (order.slot == FinisherSlot) {
    return folderRules(context).finisherTiming;
  }
  const std::optional<MoveRef> move = moveOf(context, id, order.slot);
  return move ? folder::timingOf(folderRules(context), context.moves->at(*move))
              : TechniqueTiming{.preparationFrames = folderRules(context).preparationFrames,
                                .recoveryFrames = folderRules(context).recoveryFrames};
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
  enterPhase(context, id, OrderPhase::Positioning, 0);
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
  const std::int32_t cost = folderMpCost(rules, moves, *ref, condition);
  return fighter.mp - fighter.pendingMp < cost ? OrderProblem::NotEnoughMp : OrderProblem::None;
}

} // namespace dwe::game::battle
