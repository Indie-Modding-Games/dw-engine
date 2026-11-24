#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/battle/folder.hpp>

#include <cstddef>
#include <optional>
#include <vector>

#include "battle_context.hpp"

// The Folder battle's parts (docs/architecture/folder-battle.md).
namespace dwe::game::battle {

// folder_battle.cpp
[[nodiscard]] FolderFighter& folderFighter(const BattleContext& context, FighterId id);
// The stats a fighter fights with now: the break halves ATK, VEL and DEF (§2.9).
[[nodiscard]] FighterStats folderStats(const BattleContext& context, FighterId id);

// folder_orders.cpp: the trainer's side (§2) and each commanded fighter's execution (§4).
core::Result<void> folderOrder(BattleContext& context, std::size_t slot,
                               std::optional<FighterId> target, FighterId side);
void folderCancelOrders(BattleContext& context, FighterId side);
void folderSetStance(BattleContext& context, Stance stance, FighterId side);
// §2.8, §4.6: the problem of an order of `slot` for `id` now, its target aside.
[[nodiscard]] OrderProblem folderSlotProblem(const BattleState& state, const MoveCatalog& moves,
                                             const BattleRules& rules,
                                             const PartnerCondition& condition, FighterId id,
                                             std::size_t slot);
void tickFolderCommanded(BattleContext& context, FighterId id);
// A command of the strip in the Folder battle, the flee aside: a technique or the finisher is an
// order at the current target, a stance command sets the stance, change turns to the next foe and
// your call drops the waiting orders.
core::Result<void> folderCommand(BattleContext& context, Command command, FighterId side);
// The strip of a commanded fighter in the Folder battle: every command, as a full brain's menu.
[[nodiscard]] std::vector<Command> folderMenu(const BattleState& state, FighterId id);
[[nodiscard]] Command stanceCommand(Stance stance);
// The nearest foe of `id` still standing, if any.
[[nodiscard]] std::optional<FighterId> nearestFoe(const BattleContext& context, FighterId id);

} // namespace dwe::game::battle
