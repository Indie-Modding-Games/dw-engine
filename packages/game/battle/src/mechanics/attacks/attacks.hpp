#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/battle_context.hpp"

namespace dwe::game::battle {

// The original's block spark (HitParticles, BTL_tickAttackHits): 0x11 frames of the first colour.
// A covered hit of the Folder battle shows it too.
inline constexpr std::int32_t BlockSparkFrames = 0x11;
inline constexpr std::int32_t BlockSparkKind = 0;

// Attacks reaching fighters (battle_main.c: BTL_tickAttackHits) and the gradual damage (utils.c:
// damageTick).
void tickAttacksInFlight(BattleContext& context);
// battle_effect.c: BTL_faceTargetEntity: the effects in flight that turn their attacker to its
// target on this frame do it, after the fighters moved (BTL_battleTickFrame: tickObjects).
void faceEffectTargets(BattleContext& context);
void tickAttackHits(BattleContext& context);
// battle_main.c: BTL_addBlockedAttack: notes the attack on its defender's guard; false when the
// guard took it already, which keeps its spark from showing again.
bool registerBlockedAttack(const BattleContext& context, const AttackInFlight& attack);
void drainPendingDamage(BattleContext& context);
// What a hit of `damage` does to the defender: its spark, the counters, the pending damage, its hit
// reaction and the move's status (BTL_handleHitReaction).
void landHit(BattleContext& context, const AttackInFlight& attack, std::int32_t damage);
// battle_main.c: BTL_applyBuffMove: a support move raises the stats of `id`.
void applyBuff(BattleContext& context, FighterId id, MoveRef move);
void addPendingDamage(BattleContext& context, Fighter& fighter, std::int32_t damage);
// The finisher of `id` is lost (a hit during its aura).
void cancelFinisherOf(BattleContext& context, FighterId id);
// battle_main.c: BTL_removeMoveEffect: what of the own technique of `id` is still on its way stops.
// True when part of the technique was already committed and still flies.
bool stopOwnTechnique(BattleContext& context, FighterId id);
bool counteringAttacker(const BattleContext& context, const Fighter& fighter, FighterId attacker);
Point hitPointOf(const BattleContext& context, const AttackInFlight& attack);
HitParticles::Vec3 sparkPointOf(const BattleContext& context, const AttackInFlight& attack);
// The battle_win debug command's blow: `id` takes enough damage to fall, after what a hit does
// first (BTL_tickAttackHits): its own technique and finisher stop. The AttackHit it emits carries
// no move.
void strikeDown(BattleContext& context, FighterId id);
// The attack of a fighter leaves it (its animation's release frame).
void releaseAttack(BattleContext& context, FighterId id);
// The flattened fighter's shot (battle_effect.c: BTL_addAuraProjectile, BTL_tickAuraProjectile),
// fired from where `id` stands, the way it faces; none while all the slots fly.
void fireFlattenedShot(BattleContext& context, FighterId id, MoveRef move);
// Moves each shot a frame (tickObjects, after the fighters) and lands those that touch a fighter.
void tickFlattenedShots(BattleContext& context);
// When and where the effect of `move` that `attacker` plays with clip `clip` touches `defender`,
// both where they stand now (AttackFlights); nothing when the battle knows no flight for it.
std::optional<EffectLanding> effectLanding(const BattleContext& context, FighterId attacker,
                                           FighterId defender, MoveRef move,
                                           std::optional<std::int32_t> clip);

} // namespace dwe::game::battle
