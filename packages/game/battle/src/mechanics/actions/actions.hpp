#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/battle_context.hpp"

namespace dwe::game::battle {

// What each fighter does this frame (battle_main.c: BTL_tickFighterStates).
void tickFighterStates(BattleContext& context);
// Starts the queued move against the target (battle_main.c: BTL_startQueuedMove).
void startQueuedMove(BattleContext& context, FighterId id);
// Faints a fighter at 0 HP (battle_main.c: BTL_faintDigimon).
void faint(BattleContext& context, FighterId id);
// Leaves a fighter standing and doing nothing (handleBattleIdle).
void standIdle(Fighter& fighter);

// The finisher gauge (battle_main.c: BTL_addFinisherProgress).
void addFinisherProgress(BattleContext& context, FighterId id, std::int32_t amount);
// The longest reach of a fighter's equipped moves, the finisher's too, as the squared distance the
// move table gives (battle_main.c: BTL_getMoveWithHighestDistance); 0 with none.
std::int64_t longestReach(const BattleContext& context, FighterId id);
// battle_main.c: BTL_handlePartnerMoveCommand: a commanded fighter's flee, defend and distance
// commands move it themselves; true when one did, which keeps it from attacking this frame.
bool followMoveCommand(BattleContext& context, FighterId id);
// The end of a fighter's attack, hit or block animation (battle_main.c: BTL_tickFighterAction).
void endAnimation(BattleContext& context, FighterId id);
// Starts the queued move's attack (battle_main.c: BTL_setupMoveExecution and the attack animation).
void beginAttack(BattleContext& context, FighterId id);

} // namespace dwe::game::battle
