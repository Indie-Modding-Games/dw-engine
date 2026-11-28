#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "classic/classic.hpp"
#include "mechanics/actions/actions.hpp"
#include "mechanics/attacks/attacks.hpp"
#include "mechanics/decisions/decisions.hpp"
#include "mechanics/movement/movement.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;

// battle_main.c: BTL_retargetAfterHit.
void retargetAfterHit(BattleContext& context, FighterId id, FighterId attacker) {
  if (context.state->versus()) {
    selectTarget(context, id, attacker, true);
    return;
  }
  if (context.state->isPlayerSide(id)) {
    const Fighter& partner = context.fighter(id);
    const bool turns = context.strategies->obedience->partnerTurnsToAttacker(
        context.view(id), context.disciplineOf(id));
    if (!turns && partner.target && !context.fighter(*partner.target).defeated()) {
      return;
    }
  }
  selectTarget(context, id, attacker, true);
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

// The damage strategy's damage of a hit, with the difficulty's partner share (an engine
// improvement; 100 in the original's ruleset), kept within the formula's min and max.
std::int32_t damageOf(BattleContext& context, const AttackInFlight& attack) {
  const DamageRules& rules = context.rules->damage;
  std::int32_t damage = context.strategies->damage->damage(context.view(), attack.attacker,
                                                           attack.defender, attack.move);
  const BattleState& state = *context.state;
  const std::int32_t percent = state.isPlayerSide(attack.attacker)   ? rules.partnerDealtPercent
                               : state.isPlayerSide(attack.defender) ? rules.partnerTakenPercent
                                                                     : Hundred;
  if (percent != Hundred) {
    damage = std::clamp(damage * percent / Hundred, rules.min, rules.max);
  }
  return damage;
}

void hit(BattleContext& context, const AttackInFlight& attack) {
  retargetAfterHit(context, attack.defender, attack.attacker);
  landHit(context, attack, damageOf(context, attack));
}

void block(BattleContext& context, const AttackInFlight& attack) {
  const FighterId id = attack.defender;
  Fighter& fighter = context.fighter(id);
  const HitRules& rules = context.rules->hit;
  // The block's spark (createParticleFX(0, 2, ...)), before the guard that is still up: once for
  // each attack the guard takes (BTL_addBlockedAttack), so a move the same fighter casts again
  // while the guard holds blocks without one (and without its rolls).
  if (registerBlockedAttack(context, attack) && context.particles != nullptr) {
    context.particles->spawn(context.particleRolls(), HitParticles::Spark::Block, BlockSparkKind,
                             sparkPointOf(context, attack), id, BlockSparkFrames);
  }
  if (fighter.blocking && fighter.invulnerableFrames > 0) {
    fighter.invulnerableFrames = rules.blockFrames;
    fighter.struck = false;
    return;
  }
  std::int32_t damage = 0;
  if (context.moves->at(attack.move).range == MoveRange::Melee) {
    damage = damageOf(context, attack);
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
  if (BattleCounters* counters = context.state->countersOf(id)) {
    ++counters->blocks;
  }
  lookAt(fighter, hitPointOf(context, attack));
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

} // namespace

void handleOriginalAttack(BattleContext& context, const AttackInFlight& attack) {
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
  stopOwnTechnique(context, id);
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

} // namespace dwe::game::battle
