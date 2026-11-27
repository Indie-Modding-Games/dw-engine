#include <dwe/core/log.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/battle_system.hpp"
#include "mechanics/animation/animation.hpp"
#include "mechanics/attacks/attacks.hpp"
#include "mechanics/movement/movement.hpp"
#include "mechanics/status/status.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;
// The original's hit spark (HitParticles) lives the technique's invulnerable frames + 0x10 and
// takes the technique's `special` as its colour (BTL_startHitAnimation).
constexpr std::int32_t HitSparkFrames = 0x10;

std::optional<std::array<std::int32_t, 3>> touchOf(const EffectLanding& landing) {
  if (!landing.touch) {
    return std::nullopt;
  }
  return std::array{landing.touch->x, landing.touch->y, landing.touch->z};
}

// Whom an attack released by `id` reaches when the battle cannot follow its effect. The original's
// attack objects collide with the bodies on the ground; the engine sends them to their targets: a
// melee swing to its target if it is still within reach, a shot to its target, an area move to
// every opponent of the caster (an assumption) and a support move to the caster itself.
std::vector<FighterId> reachedBy(const BattleContext& context, FighterId id, const BattleMove& move,
                                 std::optional<FighterId> target) {
  const BattleState& state = *context.state;
  switch (move.range) {
  case MoveRange::Self:
    return {id};
  case MoveRange::Area: {
    if (!state.isPlayerSide(id)) {
      if (state.allies.empty()) {
        return {PartnerId};
      }
      // In co-op every partner still standing (docs/architecture/coop-combat.md §4.2).
      std::vector<FighterId> partners;
      for (const FighterId partner : state.partnersIn()) {
        if (!state.fighters[partner].fainted) {
          partners.push_back(partner);
        }
      }
      return partners;
    }
    std::vector<FighterId> enemies;
    for (FighterId enemy = 1; enemy <= state.enemyCount(); ++enemy) {
      if (!state.fighters[enemy].fainted) {
        enemies.push_back(enemy);
      }
    }
    return enemies;
  }
  case MoveRange::Melee: {
    if (!target) {
      return {};
    }
    const Fighter& self = state.fighters.at(id);
    const Fighter& other = state.fighters.at(*target);
    const std::int64_t reach = static_cast<std::int64_t>(self.combatant.radius) +
                               other.combatant.radius + context.rules->spacing.contactMargin;
    if (distanceSquared(self.position, other.position) > reach * reach) {
      return {};
    }
    return {*target};
  }
  case MoveRange::Ranged:
  case MoveRange::None:
    break;
  }
  if (!target) {
    return {};
  }
  return {*target};
}

} // namespace

// battle_main.c: BTL_applyBuffMove, BTL_buffStats: a support move raises the caster's stats, up to
// the cap, and shows each raise over the caster (addEntityText(entity, slot, 0xb, value, icon)):
// the raise as the percentage gives it, before the cap, for each stat the move raises at all.
void applyBuff(BattleContext& context, FighterId id, MoveRef move) {
  Fighter& fighter = context.fighter(id);
  const std::optional<BuffPercents>& percents = context.moves->at(move).buff;
  if (!percents) {
    return;
  }
  const BuffPercents& buff = *percents;
  const std::int32_t cap = context.rules->limits.statCap;
  const auto raise = [&context, id, cap](std::int32_t& stat, std::int32_t percent,
                                         EventKind shown) {
    if (percent == 0) {
      return;
    }
    const std::int32_t gain = stat * percent / Hundred;
    stat = std::min(stat + gain, cap);
    context.emit({.kind = shown,
                  .fighter = id,
                  .other = std::nullopt,
                  .move = std::nullopt,
                  .amount = gain,
                  .status = StatusEffect::None,
                  .command = Command::None});
  };
  raise(fighter.stats.offense, buff.offense, EventKind::OffenseRaised);
  raise(fighter.stats.defense, buff.defense, EventKind::DefenseRaised);
  raise(fighter.stats.speed, buff.speed, EventKind::SpeedRaised);
  fighter.struck = false;
  context.emit({.kind = EventKind::Buffed,
                .fighter = id,
                .other = std::nullopt,
                .move = move,
                .amount = 0,
                .status = StatusEffect::None,
                .command = Command::None});
}

void addPendingDamage(BattleContext& context, Fighter& fighter, std::int32_t damage) {
  fighter.pendingDamage =
      std::min(fighter.pendingDamage + damage, context.rules->limits.pendingDamageCap);
}

// The finisher of a fighter that gets hit during its aura is lost (battle_main.c:
// BTL_tickAttackHits).
void cancelFinisherOf(BattleContext& context, FighterId id) {
  FinisherState& finisher = context.state->finisher;
  if (finisher.fighter == id) {
    finisher.fighter.reset();
    finisher.auraFrames = 0;
    finisher.charging = false;
    removeFinisherAura(context);
  }
}

// battle_main.c: BTL_removeMoveEffect, which BTL_tickAttackHits calls on each fighter an attack
// reaches, stops only the sub-effect of the fighter's technique that BTL_startEFE returned: what of
// it that sub-effect would still bring to a fighter reaches no one, but a part it already spawned
// (a fireball on its way) runs on and lands. The effects run after this in the frame, so the
// spawn of the frame the age counts last is already done.
bool stopOwnTechnique(BattleContext& context, FighterId id) {
  const auto committed = [](const AttackInFlight& flying) {
    return flying.committed && flying.age && *flying.age > *flying.committed;
  };
  std::erase_if(context.state->inFlight, [id, &committed](const AttackInFlight& flying) {
    return flying.attacker == id && !committed(flying);
  });
  return std::ranges::any_of(context.state->inFlight,
                             [id](const AttackInFlight& flying) { return flying.attacker == id; });
}

bool counteringAttacker(const BattleContext& context, const Fighter& fighter, FighterId attacker) {
  return fighter.attack && context.moves->at(fighter.attack->move).counter &&
         fighter.target == attacker;
}

// Where the attack's effect hit the defender (addAttackObject's position): its own point, or for a
// check that hit the body (no point) the defender's body at its radius toward the source
// (BTL_calculateAttackHitPosition): the attacker where it stands now for a followed effect, its
// place at the release otherwise.
Point hitPointOf(const BattleContext& context, const AttackInFlight& attack) {
  if (attack.hitPoint) {
    return *attack.hitPoint;
  }
  const Point source = attack.age ? context.fighter(attack.attacker).position : attack.origin;
  const Fighter& defender = context.fighter(attack.defender);
  return bodyPointToward(defender.position, source, defender.combatant.radius);
}

// Where the spark of an attack bursts (the attack object's position): where its effect touched,
// else the defender's body toward the attacker at half its height (an assumption).
HitParticles::Vec3 sparkPointOf(const BattleContext& context, const AttackInFlight& attack) {
  if (attack.touch) {
    return *attack.touch;
  }
  const Point at = hitPointOf(context, attack);
  return {at.x, -(context.fighter(attack.defender).combatant.height >> 1), -at.z};
}

void landHit(BattleContext& context, const AttackInFlight& attack, std::int32_t damage) {
  const FighterId id = attack.defender;
  Fighter& fighter = context.fighter(id);
  const BattleMove& move = context.moves->at(attack.move);
  // BTL_handleHitReaction: the spark of the hit, unless the fighter is flattened.
  if (context.particles != nullptr && !fighter.status.flattened) {
    const HitParticles::Vec3 spark = sparkPointOf(context, attack);
    core::logDebug("[port:battle] BTL_startHitAnimation (battle_main.c:2075): spark {} at ({}, {}, "
                   "{})",
                   move.special, spark[0], spark[1], spark[2]);
    context.particles->spawn(context.particleRolls(), HitParticles::Spark::Hit, move.special, spark,
                             id, move.iframes + HitSparkFrames);
  }
  context.state->counters.anyoneHit = true;
  BattleCounters* counters = context.state->countersOf(id);
  if (counters != nullptr) {
    counters->partnerHit = true;
  }
  if (counters != nullptr && fighter.stats.maxHp > 0 &&
      damage * Hundred / fighter.stats.maxHp >= context.rules->hit.bigHitPercent) {
    ++counters->bigHits;
  }
  addPendingDamage(context, fighter, damage);
  fighter.knockedBack = true;
  const Point hitPoint = hitPointOf(context, attack);
  fighter.hitFromBehind = turnForHit(fighter, hitPoint);
  core::logDebug("[port:battle] BTL_handleHitReaction (battle_main.c:3093): fighter {} at ({}, {}) "
                 "turns to ({}, {}){}",
                 id, fighter.position.x, fighter.position.z, hitPoint.x, hitPoint.z,
                 fighter.hitFromBehind ? " from behind" : "");
  fighter.struck = true;
  fighter.pose = Pose::HitReaction;
  fighter.poseFrames = context.rules->timing.hitReactionFrames;
  fighter.invulnerableFrames = move.iframes;
  context.emit({.kind = EventKind::AttackHit,
                .fighter = id,
                .other = attack.attacker,
                .move = attack.move,
                .amount = damage,
                .status = StatusEffect::None,
                .command = Command::None});
  applyMoveStatus(context, id, attack.move);
}

namespace {

// Where the effect of an attack in flight touches its defender, asked again where the fighters
// stand now: its timing, spark and facing follow the answer. False when the effect would not touch
// it.
bool refreshLanding(const BattleContext& context, AttackInFlight& attack, std::int32_t age) {
  if (context.flights == nullptr) {
    return false;
  }
  const std::string_view move = context.moves->at(attack.move).id;
  const std::optional<EffectLanding> landing =
      attack.aim ? context.flights->landingOn(move, attack.attacker, *attack.aim, attack.defender,
                                              attack.clip)
                 : context.flights->landing(move, attack.attacker, attack.defender, attack.clip);
  if (!landing) {
    return false;
  }
  attack.framesLeft = std::max(landing->frame - age, 0);
  attack.age = age;
  attack.hitPoint = landing->at;
  attack.touch = touchOf(*landing);
  attack.facingTarget = landing->facingTarget;
  attack.laterContacts = landing->laterContacts;
  attack.committed = landing->committed;
  return true;
}

// battle_effect.c: BTL_applyLineAttackHit and the other checks, run again on a later frame: the
// attack touches its defender again, unless it is still in its hit reaction (isHit) or down.
// Whatever it touches it reaches, as a first contact does.
bool touchesAgain(const BattleContext& context, const AttackInFlight& attack) {
  const Fighter& defender = context.fighter(attack.defender);
  return !defender.fainted && !defender.struck;
}

// The same attack on its way to its next contact with the defender, if its effect has one.
std::optional<AttackInFlight> nextContact(const AttackInFlight& attack) {
  if (!attack.age || attack.laterContacts.empty()) {
    return std::nullopt;
  }
  AttackInFlight next = attack;
  next.contacted = true;
  next.framesLeft = std::max(next.laterContacts.front() - *next.age, 0);
  next.laterContacts.erase(next.laterContacts.begin());
  return next;
}

// An attack leaving `id` for `defender`: it lands when the move's effect touches the defender,
// where the battle knows its flight, else after the ruleset's projectile speed.
AttackInFlight flightOf(const BattleContext& context, FighterId id, FighterId defender,
                        bool travels) {
  const Fighter& fighter = context.fighter(id);
  const ActiveAttack& attack = *fighter.attack;
  AttackInFlight flight{.attacker = id,
                        .defender = defender,
                        .move = attack.move,
                        .origin = fighter.position,
                        .framesLeft = 0,
                        .age = std::nullopt,
                        .clip = std::nullopt,
                        .hitPoint = std::nullopt,
                        .touch = std::nullopt,
                        .facingTarget = {},
                        .aim = std::nullopt,
                        .laterContacts = {},
                        .contacted = false,
                        .committed = std::nullopt};
  if (!travels) {
    return flight;
  }
  const std::optional<std::int32_t> clip = moveClipOf(context, fighter, attack.move);
  if (const std::optional<EffectLanding> landing =
          effectLanding(context, id, defender, attack.move, clip)) {
    core::logDebug(
        "[port:battle] BTL_tickEFEEngine (battle_effect.c): {} of fighter {} touches "
        "fighter {} {} away on attack frame {} (released on {}) at ({}, {})",
        context.moves->at(attack.move).id, id, defender,
        integerSqrt(distanceSquared(fighter.position, context.fighter(defender).position)),
        landing->frame, attack.frame, landing->at ? landing->at->x : fighter.position.x,
        landing->at ? landing->at->z : fighter.position.z);
    flight.framesLeft = std::max(landing->frame - attack.frame, 0);
    flight.age = attack.frame;
    flight.clip = clip;
    flight.hitPoint = landing->at;
    flight.touch = touchOf(*landing);
    flight.facingTarget = landing->facingTarget;
    flight.laterContacts = landing->laterContacts;
    flight.committed = landing->committed;
    return flight;
  }
  const std::int64_t distance =
      integerSqrt(distanceSquared(fighter.position, context.fighter(defender).position));
  flight.framesLeft =
      static_cast<std::int32_t>(distance / std::max(context.rules->timing.projectileSpeed, 1));
  return flight;
}

// battle_effect.c: BTL_applyRadiusAttackHit, BTL_applyLineAttackHit; aabb.c: findAABBHitEntity:
// an effect's hit checks go through every fighter but its source, so a blast or a beam also hits
// the attacker's allies and the opponents it was not aimed at when it touches them on its way. Only
// a flight the move's effect decides can tell: without one the attack reaches its targets alone.
void addBystanderFlights(BattleContext& context, FighterId id,
                         const std::vector<FighterId>& reached) {
  const ActiveAttack& attack = *context.fighter(id).attack;
  const FighterId aim = attack.target.value_or(reached.front());
  const std::optional<std::int32_t> clip = moveClipOf(context, context.fighter(id), attack.move);
  for (FighterId other = 0; other < context.state->fighters.size(); ++other) {
    if (!context.state->foes(id, other) || context.fighter(other).fainted ||
        std::ranges::contains(reached, other)) {
      continue;
    }
    AttackInFlight flight = flightOf(context, id, other, false);
    flight.clip = clip;
    flight.aim = aim;
    if (!refreshLanding(context, flight, attack.frame)) {
      continue;
    }
    core::logDebug(
        "[port:battle] BTL_applyRadiusAttackHit (battle_effect.c): {} of fighter {} aimed "
        "at {} touches fighter {} on attack frame {}",
        context.moves->at(attack.move).id, id, aim, other, *flight.age + flight.framesLeft);
    context.state->inFlight.push_back(std::move(flight));
  }
}

} // namespace

void strikeDown(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (fighter.fainted) {
    return;
  }
  // BTL_tickAttackHits, for the fighter an attack reaches: its technique and its finisher stop. The
  // debug blow also drops what of it was committed, so nothing of it lands after the win (#326).
  std::erase_if(context.state->inFlight,
                [id](const AttackInFlight& flying) { return flying.attacker == id; });
  fighter.knockedBack = false;
  fighter.attack.reset();
  fighter.planned = false;
  cancelFinisherOf(context, id);
  const std::int32_t damage = std::max(fighter.hp - fighter.pendingDamage, 0);
  fighter.pendingDamage = std::max(fighter.pendingDamage, fighter.hp);
  context.emit({.kind = EventKind::AttackHit,
                .fighter = id,
                .other = PartnerId,
                .move = std::nullopt,
                .amount = damage,
                .status = StatusEffect::None,
                .command = Command::None});
}

bool registerBlockedAttack(const BattleContext& context, const AttackInFlight& attack) {
  const BlockedAttack taken{.caster = attack.move == context.moves->flattenedShot()
                                          ? std::nullopt
                                          : std::optional{attack.attacker},
                            .move = attack.move};
  std::vector<BlockedAttack>& blocked = context.fighter(attack.defender).blockedAttacks;
  if (std::ranges::contains(blocked, taken)) {
    return false;
  }
  blocked.push_back(taken);
  return true;
}

std::optional<EffectLanding> effectLanding(const BattleContext& context, FighterId attacker,
                                           FighterId defender, MoveRef move,
                                           std::optional<std::int32_t> clip) {
  if (context.flights == nullptr) {
    return std::nullopt;
  }
  return context.flights->landing(context.moves->at(move).id, attacker, defender, clip);
}

void releaseAttack(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (!fighter.attack) {
    return;
  }
  const ActiveAttack& attack = *fighter.attack;
  const BattleMove& move = context.moves->at(attack.move);
  context.emit({.kind = EventKind::AttackReleased,
                .fighter = id,
                .other = attack.target,
                .move = attack.move,
                .amount = 0,
                .status = StatusEffect::None,
                .command = Command::None});
  // battle_main.c: BTL_tickFighterAction: the flattened fighter's move is its shot, which flies on
  // its own and lands on whoever it touches (BTL_addAuraProjectile).
  if (attack.move == context.moves->flattenedShot()) {
    fireFlattenedShot(context, id, attack.move);
    return;
  }
  // A followed melee move reaches its target as a shot does: its effect decides the touch.
  const std::vector<FighterId> reached = attack.followed && attack.target
                                             ? std::vector{*attack.target}
                                             : reachedBy(context, id, move, attack.target);
  if (reached.empty()) {
    context.emit({.kind = EventKind::AttackMissed,
                  .fighter = id,
                  .other = id,
                  .move = attack.move,
                  .amount = 0,
                  .status = StatusEffect::None,
                  .command = Command::None});
    return;
  }
  const bool travels =
      attack.followed || move.range == MoveRange::Ranged || move.range == MoveRange::Area;
  for (const FighterId defender : reached) {
    context.state->inFlight.push_back(flightOf(context, id, defender, travels));
  }
  if (travels) {
    addBystanderFlights(context, id, reached);
  }
}

void faceEffectTargets(BattleContext& context) {
  for (const AttackInFlight& attack : context.state->inFlight) {
    Fighter& attacker = context.fighter(attack.attacker);
    // The age already counts this frame (tickAttacksInFlight).
    if (!attack.age || attacker.fainted || attack.attacker == attack.defender ||
        !std::ranges::contains(attack.facingTarget, *attack.age - 1)) {
      continue;
    }
    lookAt(attacker, context.fighter(attack.defender).position);
  }
}

void tickAttacksInFlight(BattleContext& context) {
  std::vector<AttackInFlight>& inFlight = context.state->inFlight;
  std::vector<AttackInFlight> stillFlying;
  for (AttackInFlight& attack : inFlight) {
    if (attack.age) {
      // battle_effect.c: BTL_tickEFEEngine checks the effect's hit boxes against where the fighters
      // stand on each frame: a defender walking into the effect is touched sooner, one walking
      // away later. Where the effect would never touch it now, the last answer holds. Once it
      // touched the defender the later contacts keep their frames: the defender stands in its
      // block or its hit reaction.
      if (!attack.contacted) {
        static_cast<void>(refreshLanding(context, attack, *attack.age));
      }
      ++*attack.age;
    }
    if (attack.framesLeft > 0) {
      --attack.framesLeft;
      stillFlying.push_back(attack);
      continue;
    }
    if (std::optional<AttackInFlight> next = nextContact(attack)) {
      stillFlying.push_back(std::move(*next));
    }
    Fighter& defender = context.fighter(attack.defender);
    if (attack.contacted && !touchesAgain(context, attack)) {
      continue;
    }
    // A fighter in its hit reaction cannot be hit again (isHit): what reaches it then is lost
    // (an assumption).
    if (attack.aim && (defender.fainted || defender.struck)) {
      // BTL_applyRadiusAttackHit skips a fighter already in its hit reaction: a bystander's touch
      // is no attack on it.
      continue;
    }
    if (defender.fainted || defender.gone() ||
        (defender.struck && attack.defender != attack.attacker)) {
      context.emit({.kind = EventKind::AttackMissed,
                    .fighter = attack.defender,
                    .other = attack.attacker,
                    .move = attack.move,
                    .amount = 0,
                    .status = StatusEffect::None,
                    .command = Command::None});
      continue;
    }
    defender.incoming.push_back(attack);
    context.emit({.kind = EventKind::AttackLanded,
                  .fighter = attack.defender,
                  .other = attack.attacker,
                  .move = attack.move,
                  .amount = 0,
                  .status = StatusEffect::None,
                  .command = Command::None});
  }
  inFlight = std::move(stillFlying);
}

void tickAttackHits(BattleContext& context) {
  BattleState& state = *context.state;
  for (FighterId id = 0; id < state.fighters.size(); ++id) {
    Fighter& fighter = state.fighters[id];
    if (fighter.fainted || fighter.incoming.empty()) {
      continue;
    }
    const AttackInFlight attack = fighter.incoming.front();
    fighter.incoming.erase(fighter.incoming.begin());
    // A partner running off takes no more hits; with one partner nobody does meanwhile.
    const std::optional<std::int32_t> flee =
        state.isPlayerSide(id) || state.allies.empty() ? state.fleeFramesOf(id) : std::nullopt;
    if (flee && *flee >= context.rules->ending.fleeIgnoresHitsFrom) {
      continue;
    }
    context.system->handleAttack(context, attack);
  }
}

void drainPendingDamage(BattleContext& context) {
  for (Fighter& fighter : context.state->fighters) {
    for (const DamageTickStep& step : context.rules->damageTick) {
      if (fighter.pendingDamage > step.above) {
        fighter.pendingDamage -= step.amount;
        fighter.hp -= step.amount;
      }
    }
    if (fighter.hp <= 0) {
      fighter.hp = 0;
      fighter.pendingDamage = 0;
    }
    if (fighter.pendingMp != 0) {
      fighter.mp = std::max(fighter.mp - fighter.pendingMp, 0);
      fighter.pendingMp = 0;
    }
  }
}

} // namespace dwe::game::battle
