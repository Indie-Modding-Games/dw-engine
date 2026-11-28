#pragma once

#include <dwe/game/battle/battle.hpp>

namespace dwe::game::battle {

// The results of a finished battle for the partner and the session (tamer.c: startBattle,
// advanceBattleTime; battle_ui.c: handleBattleEndBox; partner_impl.c: handlePostBattleTiredness).
// The rolls use the battle's random numbers, in the original's order.
BattleResult settleBattle(Outcome outcome, const BattleSetup& setup, const BattleState& state,
                          const BattleRules& rules, const MoveCatalog& moves,
                          ruleset::Random& random);
// A co-op ally's part of the results (docs/architecture/coop-combat.md §6): what the battle left
// its partner, by its own stats and counters; the bits, drops and recruit are the partner's result.
BattleResult settleAlly(Outcome outcome, const BattleSetup& setup, const BattleState& state,
                        const AllyState& ally, const BattleRules& rules, const MoveCatalog& moves,
                        ruleset::Random& random);

} // namespace dwe::game::battle
