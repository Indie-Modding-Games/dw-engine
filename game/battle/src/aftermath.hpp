#pragma once

#include <dwe/game/battle/battle.hpp>

namespace dwe::game::battle {

// The results of a finished battle for the partner and the session (tamer.c: startBattle,
// advanceBattleTime; battle_ui.c: handleBattleEndBox; partner_impl.c: handlePostBattleTiredness).
// The rolls use the battle's random numbers, in the original's order.
BattleResult settleBattle(Outcome outcome, const BattleSetup& setup, const BattleState& state,
                          const BattleRules& rules, const MoveCatalog& moves,
                          ruleset::Random& random);

} // namespace dwe::game::battle
