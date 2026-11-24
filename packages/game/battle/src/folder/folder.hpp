#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/battle/folder/state.hpp>

#include <cstddef>
#include <memory>
#include <optional>

#include "core/battle_context.hpp"
#include "core/battle_system.hpp"

// The Folder battle (docs/architecture/folder-battle.md): what the rest of the battle asks of it.
// Its parts live beside this file: the strip's commands, the orders, the stances and the defense.
namespace dwe::game::battle {

[[nodiscard]] std::unique_ptr<BattleSystem> makeFolderBattle();

// orders/order_book.cpp: the trainer's orders (§2).
core::Result<void> folderOrder(BattleContext& context, std::size_t slot,
                               std::optional<FighterId> target, FighterId side);
void folderCancelOrders(BattleContext& context, FighterId side);
// orders/order_checks.cpp, §2.8, §4.6: the problem of an order of `slot` for `id` now, its target
// aside.
[[nodiscard]] OrderProblem folderSlotProblem(const BattleState& state, const MoveCatalog& moves,
                                             const BattleRules& rules,
                                             const PartnerCondition& condition, FighterId id,
                                             std::size_t slot);
// stances/stances.cpp
void folderSetStance(BattleContext& context, Stance stance, FighterId side);

} // namespace dwe::game::battle
