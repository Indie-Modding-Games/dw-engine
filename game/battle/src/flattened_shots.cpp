#include <dwe/core/log.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>

#include "battle_context.hpp"

namespace dwe::game::battle {

namespace {

// battle_effect.c: BTL_addAuraProjectile: the shot leaves half the fighter's height up and as far
// ahead of it (the species height * 50 / 100).
constexpr std::int32_t HalfPercent = 50;
constexpr std::int32_t Hundred = 100;

// `length` units the way `facing` looks, as ApplyMatrixSV rounds them (wholeUnits).
Point along(Facing facing, std::int32_t length) {
  return wholeUnits(static_cast<std::int64_t>(facing.x) * length,
                    static_cast<std::int64_t>(facing.z) * length);
}

// aabb.c: findAABBHitEntity, hasAABBOverlap: the first fighter but the shooter whose box touches
// the shot's, edges included. A fighter's box is half its radius across each way and its height
// tall from the ground; the shot's is the ruleset's half sizes around it. The original also goes
// through the fallen (suposición: the engine leaves them out, as the effects' hit checks do).
std::optional<FighterId> touchedBy(const BattleContext& context, const FlattenedShot& shot) {
  const PacingRules& pacing = context.rules->pacing;
  const BattleState& state = *context.state;
  for (FighterId id = 0; id < state.fighters.size(); ++id) {
    const Fighter& fighter = state.fighters[id];
    if (id == shot.attacker || fighter.fainted) {
      continue;
    }
    const std::int32_t bodyHalfHeight = fighter.combatant.height >> 1;
    const std::int32_t across = pacing.flattenedShotHalfWidth + (fighter.combatant.radius >> 1);
    const std::int32_t up = pacing.flattenedShotHalfHeight + bodyHalfHeight;
    if (std::abs(shot.at.x - fighter.position.x) <= across &&
        std::abs(shot.at.z - fighter.position.z) <= across &&
        std::abs(shot.height - bodyHalfHeight) <= up) {
      return id;
    }
  }
  return std::nullopt;
}

// BTL_tickAuraProjectile: addAttackObject at the shot, which BTL_tickAttackHits takes on the next
// frame as any other hit.
void land(BattleContext& context, const FlattenedShot& shot, FighterId defender) {
  context.fighter(defender).incoming.push_back(
      AttackInFlight{.attacker = shot.attacker,
                     .defender = defender,
                     .move = shot.move,
                     .origin = context.fighter(shot.attacker).position,
                     .framesLeft = 0,
                     .age = std::nullopt,
                     .clip = std::nullopt,
                     .hitPoint = shot.at,
                     .facingTarget = {}});
  context.emit({.kind = EventKind::AttackLanded,
                .fighter = defender,
                .other = shot.attacker,
                .move = shot.move,
                .amount = 0,
                .status = StatusEffect::None,
                .command = Command::None});
}

} // namespace

void fireFlattenedShot(BattleContext& context, FighterId id, MoveRef move) {
  std::array<std::optional<FlattenedShot>, FlattenedShotSlots>& shots =
      context.state->flattenedShots;
  const auto free = std::ranges::find_if(
      shots, [](const std::optional<FlattenedShot>& shot) { return !shot.has_value(); });
  if (free == shots.end()) {
    core::logDebug("[port:battle] BTL_addAuraProjectile (battle_effect.c): fighter {} fires no "
                   "shot, all {} fly",
                   id, FlattenedShotSlots);
    return;
  }
  const Fighter& fighter = context.fighter(id);
  const std::int32_t half = fighter.combatant.height * HalfPercent / Hundred;
  const Point ahead = along(fighter.facing, half);
  *free =
      FlattenedShot{.attacker = id,
                    .move = move,
                    .at = {.x = fighter.position.x + ahead.x, .z = fighter.position.z + ahead.z},
                    .height = half,
                    .step = along(fighter.facing, context.rules->pacing.flattenedShotSpeed),
                    .facing = fighter.facing,
                    .frame = 0};
}

void tickFlattenedShots(BattleContext& context) {
  const std::int32_t life = context.rules->pacing.flattenedShotLifeFrames;
  for (std::optional<FlattenedShot>& slot : context.state->flattenedShots) {
    if (!slot) {
      continue;
    }
    FlattenedShot& shot = *slot;
    ++shot.frame;
    if (shot.frame >= life) {
      slot.reset();
      continue;
    }
    shot.at.x += shot.step.x;
    shot.at.z += shot.step.z;
    const std::optional<FighterId> touched = touchedBy(context, shot);
    // A fighter in its hit reaction (isHit) stops nothing: the shot flies on, and the next frame
    // finds the same fighter first again.
    if (!touched || context.fighter(*touched).struck) {
      continue;
    }
    land(context, shot, *touched);
    slot.reset();
  }
}

} // namespace dwe::game::battle
