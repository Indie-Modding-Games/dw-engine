#pragma once

#include <dwe/core/error.hpp>

#include <memory>

#include "core/battle_context.hpp"
#include "core/battle_system.hpp"

// The original's battle, bit for bit: the trainer's commands steer the partner's own decisions, and
// the hits resolve as BTL_tickAttackHits does.
namespace dwe::game::battle {

[[nodiscard]] std::unique_ptr<BattleSystem> makeClassicBattle();

// strip.cpp: a command of the strip, the flee aside (battle_tick.c: handleCommands).
core::Result<void> classicCommand(BattleContext& context, Command command, FighterId side);

// commanded_decisions.cpp (battle_main.c: BTL_tickPartnerAI, BTL_tickEnemyAI); in a versus match
// the rival decides as the partner (vs_combat.c: VS__tickDigimonAI) within tickEnemyAi.
void tickPartnerAi(BattleContext& context);
void tickEnemyAi(BattleContext& context);

// attack_resolution.cpp: an attack that reached its defender: counter, support, hit chance, hit or
// block (battle_main.c: BTL_tickAttackHits).
void handleOriginalAttack(BattleContext& context, const AttackInFlight& attack);

} // namespace dwe::game::battle
