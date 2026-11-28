#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/battle_context.hpp"

// An order's life (combat-design.md §4): it waits in the queue, then goes through processing,
// positioning, preparation, execution and recovery.
namespace dwe::game::battle {

// order_book.cpp: the queue and the end of an order.
void emitOrder(const BattleContext& context, EventKind kind, FighterId id, const Order& order,
               std::int32_t amount);
void enterPhase(const BattleContext& context, FighterId id, OrderPhase phase, std::int32_t frames);
// The last order that ended; one that did not finish becomes the HUD's notice (§10.4).
void remember(FolderFighter& own, const Order& order, OrderEnd end, OrderProblem problem);
void endOrder(BattleContext& context, FighterId id, OrderEnd end, OrderProblem problem);
// §2.8, §4.5: an interrupted order waits again at the front of the queue; a full queue drops its
// last order for it.
void interrupt(BattleContext& context, FighterId id);
// §4.5: a hit cut the order's preparation or its technique; the order stays and is retried from
// positioning once the hit reaction ends.
void setBack(BattleContext& context, FighterId id);

// order_checks.cpp
[[nodiscard]] std::optional<MoveRef> moveOf(const BattleContext& context, FighterId id,
                                            std::size_t slot);
// §5: how long the order's technique prepares and recovers.
[[nodiscard]] TechniqueTiming timingOf(const BattleContext& context, FighterId id,
                                       const Order& order);
// The end of processing (§4.2 steps 2 and 3): the order's target and its validation.
void validate(BattleContext& context, FighterId id);
// The order still stands while positioning and preparing: its target and its MP.
[[nodiscard]] bool stillValid(BattleContext& context, FighterId id);

// order_cycle.cpp: a commanded fighter's frame, its active order's phase by phase.
void tickFolderCommanded(BattleContext& context, FighterId id);

} // namespace dwe::game::battle
