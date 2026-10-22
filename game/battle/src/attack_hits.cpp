#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "battle_context.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;

// Whom an attack released by `id` reaches. The original's attack objects collide with the bodies on
// the ground; the engine sends them to their targets: a melee swing to its target if it is still
// within reach, a shot to its target, an area move to every opponent of the caster (suposición) and
// a support move to the caster itself.
std::vector<FighterId> reachedBy(const BattleContext& context, FighterId id, const BattleMove& move,
                                 std::optional<FighterId> target) {
  const BattleState& state = *context.state;
  switch (move.range) {
  case MoveRange::Self:
    return {id};
  case MoveRange::Area: {
    if (id != PartnerId) {
      return {PartnerId};
    }
    std::vector<FighterId> enemies;
    for (FighterId enemy = 1; enemy < state.fighters.size(); ++enemy) {
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

// battle_main.c: BTL_applyBuffMove, BTL_buffStats: a support move raises the caster's stats, up to
// the cap.
void applyBuff(BattleContext& context, FighterId id, MoveRef move) {
  Fighter& fighter = context.fighter(id);
  const std::optional<BuffPercents>& percents = context.moves->at(move).buff;
  if (!percents) {
    return;
  }
  const BuffPercents& buff = *percents;
  const std::int32_t cap = context.rules->limits.statCap;
  const auto raise = [cap](std::int32_t& stat, std::int32_t percent) {
    stat = std::min(stat + (stat * percent / Hundred), cap);
  };
  raise(fighter.stats.offense, buff.offense);
  raise(fighter.stats.defense, buff.defense);
  raise(fighter.stats.speed, buff.speed);
  fighter.struck = false;
  context.emit({.kind = EventKind::Buffed,
                .fighter = id,
                .other = std::nullopt,
                .move = move,
                .amount = 0,
                .status = StatusEffect::None,
                .command = Command::None});
}

// battle_main.c: BTL_retargetAfterHit.
void retargetAfterHit(BattleContext& context, FighterId id, FighterId attacker) {
  if (id == PartnerId) {
    const Fighter& partner = context.fighter(id);
    const bool turns = context.strategies->obedience->partnerTurnsToAttacker(
        context.view(), context.condition->discipline);
    if (!turns && partner.target && !context.fighter(*partner.target).defeated()) {
      return;
    }
  }
  selectTarget(context, id, attacker, true);
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
  }
}

bool counteringAttacker(const BattleContext& context, const Fighter& fighter, FighterId attacker) {
  return fighter.attack && context.moves->at(fighter.attack->move).counter &&
         fighter.target == attacker;
}

// battle_main.c: BTL_tickAttackHits: a melee attack that misses may be countered.
bool tryCounter(BattleContext& context, FighterId id, FighterId attacker) {
  Fighter& fighter = context.fighter(id);
  const bool needsMp = context.rules->quirks.on(quirk::CounterNeedsMp);
  for (std::size_t slot = 0; slot < EquippedSlots; ++slot) {
    if (needsMp && fighter.mp < context.rules->hit.counterMinMp) {
      break;
    }
    const std::optional<MoveRef> move = fighter.moves.at(slot);
    if (!move || !context.moves->at(*move).counter) {
      continue;
    }
    const std::int32_t level = std::max(fighter.combatant.level, 1);
    if (context.random->below(Hundred) < fighter.stats.speed / level) {
      fighter.queuedMove = move;
      setTarget(context, id, attacker);
      fighter.moveRange = static_cast<std::int32_t>(MoveRange::Melee);
      context.emit({.kind = EventKind::Countered,
                    .fighter = id,
                    .other = attacker,
                    .move = move,
                    .amount = 0,
                    .status = StatusEffect::None,
                    .command = Command::None});
      startQueuedMove(context, id);
      fighter.struck = false;
      return true;
    }
  }
  return false;
}

void hit(BattleContext& context, const AttackInFlight& attack) {
  const FighterId id = attack.defender;
  Fighter& fighter = context.fighter(id);
  const BattleMove& move = context.moves->at(attack.move);
  retargetAfterHit(context, id, attack.attacker);
  const std::int32_t damage =
      context.strategies->damage->damage(context.view(), attack.attacker, id, attack.move);
  context.state->counters.anyoneHit = true;
  context.state->counters.partnerHit = context.state->counters.partnerHit || id == PartnerId;
  if (id == PartnerId && fighter.stats.maxHp > 0 &&
      damage * Hundred / fighter.stats.maxHp >= context.rules->hit.bigHitPercent) {
    ++context.state->counters.bigHits;
  }
  addPendingDamage(context, fighter, damage);
  fighter.knockedBack = true;
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

void block(BattleContext& context, const AttackInFlight& attack) {
  const FighterId id = attack.defender;
  Fighter& fighter = context.fighter(id);
  const HitRules& rules = context.rules->hit;
  if (fighter.blocking && fighter.invulnerableFrames > 0) {
    fighter.invulnerableFrames = rules.blockFrames;
    fighter.struck = false;
    return;
  }
  std::int32_t damage = 0;
  if (context.moves->at(attack.move).range == MoveRange::Melee) {
    damage = context.strategies->damage->damage(context.view(), attack.attacker, id, attack.move);
    const std::int32_t span = rules.blockedMeleeMaxPercent - rules.blockedMeleeMinPercent + 1;
    damage = std::max(
        damage * (context.random->below(span) + rules.blockedMeleeMinPercent) / Hundred, 1);
    addPendingDamage(context, fighter, damage);
  }
  const FinisherRules& finisher = context.rules->finisher;
  addFinisherProgress(context, id,
                      fighter.finisherGoal * finisher.perBlock / finisher.progressDivisor);
  const Fighter& attacker = context.fighter(attack.attacker);
  addFinisherProgress(context, attack.attacker,
                      attacker.finisherGoal * finisher.perBlockedAttack / finisher.progressDivisor);
  if (id == PartnerId) {
    ++context.state->counters.blocks;
  }
  lookAt(fighter, attack.origin);
  fighter.blocking = true;
  fighter.pose = Pose::Blocking;
  fighter.poseFrames = context.rules->timing.blockAnimationFrames;
  fighter.invulnerableFrames = rules.blockFrames;
  fighter.struck = false;
  context.emit({.kind = EventKind::AttackBlocked,
                .fighter = id,
                .other = attack.attacker,
                .move = attack.move,
                .amount = damage,
                .status = StatusEffect::None,
                .command = Command::None});
}

void handleAttack(BattleContext& context, const AttackInFlight& attack) {
  const FighterId id = attack.defender;
  Fighter& fighter = context.fighter(id);
  if (counteringAttacker(context, fighter, attack.attacker)) {
    fighter.struck = false;
    return;
  }
  const BattleMove& move = context.moves->at(attack.move);
  if (move.buff) {
    applyBuff(context, id, attack.move);
    return;
  }
  const HitChance chance =
      context.strategies->damage->hitChance(context.view(), attack.attacker, id, attack.move);
  if (chance.breaksBlock) {
    fighter.blocking = false;
    fighter.invulnerableFrames = 0;
  }
  fighter.knockedBack = false;
  fighter.attack.reset();
  fighter.planned = false;
  cancelFinisherOf(context, id);
  if (context.random->below(Hundred) < chance.percent) {
    hit(context, attack);
    return;
  }
  const bool countered = !fighter.blocking && move.range == MoveRange::Melee &&
                         tryCounter(context, id, attack.attacker);
  if (!countered) {
    block(context, attack);
  }
}

} // namespace

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
  const std::vector<FighterId> reached = reachedBy(context, id, move, attack.target);
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
  const bool travels = move.range == MoveRange::Ranged || move.range == MoveRange::Area;
  for (const FighterId defender : reached) {
    std::int32_t frames = 0;
    if (travels) {
      const std::int64_t distance =
          integerSqrt(distanceSquared(fighter.position, context.fighter(defender).position));
      frames =
          static_cast<std::int32_t>(distance / std::max(context.rules->timing.projectileSpeed, 1));
    }
    context.state->inFlight.push_back({.attacker = id,
                                       .defender = defender,
                                       .move = attack.move,
                                       .origin = fighter.position,
                                       .framesLeft = frames});
  }
}

void tickAttacksInFlight(BattleContext& context) {
  std::vector<AttackInFlight>& inFlight = context.state->inFlight;
  std::vector<AttackInFlight> stillFlying;
  for (AttackInFlight& attack : inFlight) {
    if (attack.framesLeft > 0) {
      --attack.framesLeft;
      stillFlying.push_back(attack);
      continue;
    }
    Fighter& defender = context.fighter(attack.defender);
    // A fighter in its hit reaction cannot be hit again (isHit): what reaches it then is lost
    // (suposición).
    if (defender.fainted || (defender.struck && attack.defender != attack.attacker)) {
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
    if (state.fleeFrames && *state.fleeFrames >= context.rules->ending.fleeIgnoresHitsFrom) {
      continue;
    }
    handleAttack(context, attack);
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
