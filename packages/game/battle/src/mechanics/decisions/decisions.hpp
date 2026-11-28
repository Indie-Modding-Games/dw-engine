#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/battle_context.hpp"

// The decisions both battle systems share: timers, the enemies' decisions, move plans and targets.
// Who decides for the commanded fighters is each system's own (classic/, folder/).
namespace dwe::game::battle {

// fighter_upkeep.cpp
[[nodiscard]] std::size_t randomIndex(ruleset::Random& random, std::size_t count);
// The obedience roll, the cooldown and the speed reserve of a frame, which a finisher's freeze
// stops.
void tickTimers(BattleContext& context, FighterId id, std::int32_t discipline);
// A status or a faint keeps the fighter from following its trainer.
[[nodiscard]] bool isHeld(const Fighter& fighter);
void clearStatusFlags(Fighter& fighter);

// enemy_decisions.cpp (battle_main.c: BTL_tickEnemyAI)
// `stop` turns true when a finisher's freeze stops the enemies after it.
void tickEnemy(BattleContext& context, FighterId id, bool& stop);
// The enemies' decisions alone, the commanded fighters left to the battle system (Folder).
void tickEnemyDecisions(BattleContext& context);
// The buff priorities wear off with time (partner_ai.buff_priority_decay_frames).
void decayBuffPriorities(BattleContext& context);
// Every partner in the battle is at 0 HP: the enemies stand idle (with one partner, the partner).
[[nodiscard]] bool playersDown(const BattleState& state);

// move_plans.cpp
// Plans a move of a slot and its charge requirement (battle_main.c: BTL_setupQueuedMove).
void queueMove(BattleContext& context, FighterId id, std::size_t slot);
// battle_main.c: BTL_applyChargeRequirement.
void applyChargeRequirement(BattleContext& context, FighterId id, MoveRef move);
// Waits with no move: a cooldown that also charges (battle_main.c: BTL_setFighterCooldown).
void waitWithoutMove(BattleContext& context, FighterId id);
// A random target and usable move for a confused fighter (battle_main.c: BTL_selectConfusedMove).
void chooseConfusedMove(BattleContext& context, FighterId id);
// Cancels the waits and the disobedience (battle_main.c: BTL_resetFighterAction).
void resetAction(Fighter& fighter);

// targets.cpp
void setTarget(BattleContext& context, FighterId id, std::optional<FighterId> target);
// The target a fighter turns to after an attack of `attacker` or when its confusion ends
// (battle_main.c: BTL_selectFighterTarget).
void selectTarget(BattleContext& context, FighterId id, std::optional<FighterId> toward,
                  bool forced);
// The partner an enemy goes for when nothing else draws it; in co-op the one it targets while that
// stands, else the nearest standing one (docs/architecture/coop-combat.md §4.2).
[[nodiscard]] FighterId enemyTargetOf(const BattleContext& context, FighterId enemy);

} // namespace dwe::game::battle
