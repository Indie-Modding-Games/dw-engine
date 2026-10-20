#pragma once

#include <dwe/game/battle/battle.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace dwe::game::battle {

// Everything one frame of the battle works on, passed to the parts of the loop.
struct BattleContext {
    const BattleRules* rules = nullptr;
    const MoveCatalog* moves = nullptr;
    BattleState* state = nullptr;
    BattleStrategies* strategies = nullptr;
    ruleset::Random* random = nullptr;
    BattleEvents* events = nullptr;
    const PartnerCondition* condition = nullptr;

    [[nodiscard]] BattleView view() const {
        return {.rules = rules,
                .moves = moves,
                .state = state,
                .random = random,
                .partnerSick = condition->sick,
                .partnerInjured = condition->injured};
    }
    [[nodiscard]] Fighter& fighter(FighterId id) const { return state->fighters.at(id); }
    void emit(BattleEvent event) const { events->push_back(event); }
};

// ai_ticks.cpp: the partner's and the enemies' decisions of a frame (battle_main.c: BTL_tickPartnerAI,
// BTL_tickEnemyAI).
void tickPartnerAi(BattleContext& context);
void tickEnemyAi(BattleContext& context);

// Plans a move of a slot and its charge requirement (battle_main.c: BTL_setupQueuedMove).
void queueMove(BattleContext& context, FighterId id, std::size_t slot);
// battle_main.c: BTL_applyChargeRequirement.
void applyChargeRequirement(BattleContext& context, FighterId id, MoveRef move);
// Waits with no move: a cooldown that also charges (battle_main.c: BTL_setFighterCooldown).
void waitWithoutMove(BattleContext& context, FighterId id);
// A random target and a random usable move for a confused fighter (battle_main.c: BTL_selectConfusedMove).
void chooseConfusedMove(BattleContext& context, FighterId id);
// The target a fighter turns to after an attack of `attacker` or when its confusion ends
// (battle_main.c: BTL_selectFighterTarget).
void selectTarget(BattleContext& context, FighterId id, std::optional<FighterId> toward, bool forced);
// Cancels the waits and the disobedience (battle_main.c: BTL_resetFighterAction).
void resetAction(Fighter& fighter);
// Sets a fighter's target and reports it.
void setTarget(BattleContext& context, FighterId id, std::optional<FighterId> target);

// fighter_actions.cpp: what each fighter does this frame (battle_main.c: BTL_tickFighterStates).
void tickFighterStates(BattleContext& context);
// Starts the queued move against the target (battle_main.c: BTL_startQueuedMove).
void startQueuedMove(BattleContext& context, FighterId id);
// Faints a fighter at 0 HP (battle_main.c: BTL_faintDigimon).
void faint(BattleContext& context, FighterId id);
// Leaves a fighter standing and doing nothing (handleBattleIdle).
void standIdle(Fighter& fighter);

// attack_hits.cpp: attacks reaching fighters and their results (battle_main.c: BTL_tickAttackHits), and the
// gradual damage (utils.c: damageTick).
void tickAttacksInFlight(BattleContext& context);
void tickAttackHits(BattleContext& context);
void drainPendingDamage(BattleContext& context);
// The attack of a fighter leaves it (its animation's release frame).
void releaseAttack(BattleContext& context, FighterId id);

// status_effects.cpp (battle_main.c: BTL_applyMoveStatus, BTL_tickStatusEffects, BTL_healStatusEffect).
void applyMoveStatus(BattleContext& context, FighterId id, MoveRef move);
void tickStatusEffects(BattleContext& context);
void cureStatus(BattleContext& context, FighterId id);

// The finisher gauge (battle_main.c: BTL_addFinisherProgress).
void addFinisherProgress(BattleContext& context, FighterId id, std::int32_t amount);

// movement.cpp: walking on the battle ground. Distances squared, as the original compares them.
std::int64_t contactRangeSquared(const BattleContext& context, FighterId a, FighterId b);
void lookAt(Fighter& fighter, Point point);
void walkToward(BattleContext& context, FighterId id, Point point);
void backAwayFrom(BattleContext& context, FighterId id, Point point);
// battle_main.c: BTL_maintainTargetDistance.
void keepDistance(BattleContext& context, FighterId id, FighterId target);
// battle_main.c: BTL_confusedRotate: a confused fighter turns at random.
void turnAtRandom(BattleContext& context, Fighter& fighter);
// Walks the way it faces.
void walkForward(BattleContext& context, FighterId id);
bool isWalking(const Fighter& fighter);
// The integer square root, rounded down.
std::int64_t integerSqrt(std::int64_t value);
// battle_main.c: BTL_handlePartnerMoveCommand: the partner's flee, defend and distance commands move it themselves;
// true when one did, which keeps it from attacking this frame.
bool followMoveCommand(BattleContext& context);
// The end of a fighter's attack, hit or block animation (battle_main.c: BTL_tickFighterAction).
void endAnimation(BattleContext& context, FighterId id);
// Starts the attack of the queued move (battle_main.c: BTL_setupMoveExecution and the attack animation).
void beginAttack(BattleContext& context, FighterId id);

} // namespace dwe::game::battle
