#pragma once

#include <dwe/core/error.hpp>

#include <vector>

#include "core/battle_context.hpp"

// The command strip in the Folder battle: every command, as a full brain's menu.
namespace dwe::game::battle {

[[nodiscard]] std::vector<Command> folderMenu(const BattleState& state, FighterId id);
// A command of the strip, the flee aside: a technique or the finisher is an order at the current
// target, a stance command sets the stance, change turns to the next foe and your call drops the
// waiting orders.
core::Result<void> folderCommand(BattleContext& context, Command command, FighterId side);

} // namespace dwe::game::battle
