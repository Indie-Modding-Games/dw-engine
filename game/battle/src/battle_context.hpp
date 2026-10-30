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
  const BattleGround* ground = nullptr;
  const AttackFlights* flights = nullptr;
  // With the original's rolls only: the draws of its hit sparks (Battle::useOriginalRolls).
  ParticleRolls* particles = nullptr;

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

// ai_ticks.cpp: the partner's and the enemies' decisions of a frame (battle_main.c:
// BTL_tickPartnerAI, BTL_tickEnemyAI).
void tickPartnerAi(BattleContext& context);
void tickEnemyAi(BattleContext& context);

// Plans a move of a slot and its charge requirement (battle_main.c: BTL_setupQueuedMove).
void queueMove(BattleContext& context, FighterId id, std::size_t slot);
// battle_main.c: BTL_applyChargeRequirement.
void applyChargeRequirement(BattleContext& context, FighterId id, MoveRef move);
// Waits with no move: a cooldown that also charges (battle_main.c: BTL_setFighterCooldown).
void waitWithoutMove(BattleContext& context, FighterId id);
// A random target and a random usable move for a confused fighter (battle_main.c:
// BTL_selectConfusedMove).
void chooseConfusedMove(BattleContext& context, FighterId id);
// The target a fighter turns to after an attack of `attacker` or when its confusion ends
// (battle_main.c: BTL_selectFighterTarget).
void selectTarget(BattleContext& context, FighterId id, std::optional<FighterId> toward,
                  bool forced);
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

// attack_hits.cpp: attacks reaching fighters and their results (battle_main.c: BTL_tickAttackHits),
// and the gradual damage (utils.c: damageTick).
void tickAttacksInFlight(BattleContext& context);
void tickAttackHits(BattleContext& context);
void drainPendingDamage(BattleContext& context);
// The attack of a fighter leaves it (its animation's release frame).
void releaseAttack(BattleContext& context, FighterId id);
// When and where the effect of `move` that `attacker` plays with its clip `clip` touches
// `defender`, both where they stand now (AttackFlights); nothing when the battle knows no flight
// for it.
std::optional<EffectLanding> effectLanding(const BattleContext& context, FighterId attacker,
                                           FighterId defender, MoveRef move,
                                           std::optional<std::int32_t> clip);

// status_effects.cpp (battle_main.c: BTL_applyMoveStatus, BTL_tickStatusEffects,
// BTL_healStatusEffect).
void applyMoveStatus(BattleContext& context, FighterId id, MoveRef move);
void tickStatusEffects(BattleContext& context);
void cureStatus(BattleContext& context, FighterId id);

// The finisher gauge (battle_main.c: BTL_addFinisherProgress).
void addFinisherProgress(BattleContext& context, FighterId id, std::int32_t amount);

// thrown_item.cpp: the tamer's thrown item (main.c: startThrowingItem, tickThrownItem) and what it
// does when it lands (item.c), and the HUD's HP bars (battle_main.c: BTL_renderEnemyHPBars).
void throwItem(BattleContext& context, const ThrownItem& item);
void tickThrownItem(BattleContext& context);
// HP and MP healed, each up to its maximum; nothing while the partner is down.
void restorePartner(Fighter& partner, std::int32_t hp, std::int32_t mp);
// Gets the fallen partner up with `hp` and resets every fighter's action (battle_setup.c:
// BTL_isBattleFinished).
void revivePartner(BattleContext& context, std::int32_t hp);
// An enemy's HP bar shows again on damage and counts the frames after its hit reaction and its
// damage drain.
void tickHpBars(BattleContext& context);

// The original's animation ids for the battle, the same for every species.
inline constexpr std::int32_t DisobeyClip = 0x13;
inline constexpr std::int32_t StanceClip = 0x21;
inline constexpr std::int32_t WeakStanceClip = 0x22;
inline constexpr std::int32_t WeakWalkClip = 0x23;
inline constexpr std::int32_t WalkClip = 0x24;
inline constexpr std::int32_t BlockClip = 0x25;
inline constexpr std::int32_t HitClip = 0x28;
inline constexpr std::int32_t HitFromBehindClip = 0x29;
inline constexpr std::int32_t FaintClip = 0x2B;

// shown_clips.cpp: the animation each fighter shows, started as the original starts it
// (startAnimation): the stances when the battle starts, then each frame from the poses and the
// frame's events.
void startShownClips(BattleState& state);
void updateShownClips(const BattleContext& context);
// battle_main.c: BTL_setupQueuedMove: the species' clip of a move (FirstMoveAnimation plus the
// move's place in the species' list); nothing for a move outside the list.
std::optional<std::int32_t> moveClipOf(const BattleContext& context, const Fighter& fighter,
                                       std::optional<MoveRef> move);
// The length in frames of one of the fighter's clips; nothing when the battle has no motion for it.
std::optional<std::int32_t> clipLength(const Fighter& fighter, std::int32_t clip);
// Whether `clip`, which the fighter must be showing, has played to its end (anim.c: animFlag bit 0
// off): false while it shows another clip, nothing when the battle has no motion for it.
std::optional<bool> clipEnded(const Fighter& fighter, std::int32_t clip);
// The root step of the fighter's clip on its next frame; nothing once the clip ended or without
// its motion.
std::optional<RootStep> nextRootStep(const Fighter& fighter);

// movement.cpp: walking on the battle ground. Distances squared, as the original compares them.
std::int64_t contactRangeSquared(const BattleContext& context, FighterId a, FighterId b);
void lookAt(Fighter& fighter, Point point);
// battle_main.c: BTL_handleHitReaction: a fighter hit from the front (within a quarter turn of
// where it looks) or while flattened turns to the attack; one hit from behind turns its back to it.
// True for the latter.
bool turnForHit(Fighter& fighter, Point attack);
// The walks stop at whatever the step would run into (map.c: entityCheckCollision): the combat
// area, the map and the other fighters. battle_main.c: BTL_moveTowardLocation: blocked, the fighter
// keeps its facing and turns a little toward a free way (collisionGrace).
void walkToward(BattleContext& context, FighterId id, Point point);
// battle_main.c: BTL_tickMeleeAttack walks up to the target without the collision test.
void approach(BattleContext& context, FighterId id, Point point);
// battle_main.c: BTL_backAwayFromTarget: turns its back on the target and walks away.
void backAwayFrom(BattleContext& context, FighterId id, FighterId target);
// battle_main.c: BTL_maintainTargetDistance.
void keepDistance(BattleContext& context, FighterId id, FighterId target);
// battle_main.c: BTL_confusedRotate: a confused fighter turns at random.
void turnAtRandom(BattleContext& context, Fighter& fighter);
// Walks the way it faces.
void walkForward(BattleContext& context, FighterId id);
bool isWalking(const Fighter& fighter);
// battle_main.c: BTL_tickFighterAction: a fighter with clips holds this frame's root step when the
// step ahead runs into something (entityCheckCollision with the combat area).
void testRootStep(BattleContext& context, FighterId id);
// anim.c: tickAnimation for every fighter with clips: its clip plays a frame (unless frozen) and
// its root step moves it along its facing, unless held (applyRootMomentum).
void tickRootMotion(BattleContext& context);
// map.c: entityCheckCombatArea: whether the step of a mover of `radius` from `from` to `to` leaves
// the rectangle of width x height pixels around the screen's centre, moving further out.
bool leavesScreenArea(const BattleGround& ground, const TimingRules& timing, Point from, Point to,
                      std::int32_t radius, std::int32_t width, std::int32_t height);
// The integer square root, rounded down.
std::int64_t integerSqrt(std::int64_t value);
// battle_main.c: BTL_handlePartnerMoveCommand: the partner's flee, defend and distance commands
// move it themselves; true when one did, which keeps it from attacking this frame.
bool followMoveCommand(BattleContext& context);
// The end of a fighter's attack, hit or block animation (battle_main.c: BTL_tickFighterAction).
void endAnimation(BattleContext& context, FighterId id);
// Starts the attack of the queued move (battle_main.c: BTL_setupMoveExecution and the attack
// animation).
void beginAttack(BattleContext& context, FighterId id);

} // namespace dwe::game::battle
