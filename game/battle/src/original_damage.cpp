#include <algorithm>
#include <cstddef>

#include "original_strategies.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;

bool isAttacking(const BattleState& state, FighterId fighter) {
  return state.fighters.at(fighter).attack.has_value() || state.finisher.fighter == fighter;
}

// battle_main.c: BTL_calculateHitChance.
class OriginalDamage final : public DamageFormula {
public:
  HitChance hitChance(const BattleView& view, FighterId attacker, FighterId defender,
                      MoveRef ref) override {
    const BattleRules& rules = *view.rules;
    const Fighter& them = view.state->fighters.at(defender);
    const Fighter& me = view.state->fighters.at(attacker);
    const BattleMove& move = view.moves->at(ref);
    constexpr HitChance Certain{.percent = Hundred, .breaksBlock = false};
    // std_main.c: STD_applyPartnerStatsToFighter, the tournament's copy: no hit lands once a
    // fighter has fainted, and a low level is no sure hit.
    const bool match = view.state->matchClock.has_value();
    if (match && view.state->hitsOff) {
      return {.percent = 0, .breaksBlock = false};
    }
    if (!match && them.combatant.level < rules.hit.alwaysHitBelowLevel) {
      return Certain;
    }
    if (them.status.stunned || them.status.flattened || them.disobeying) {
      return Certain;
    }
    const bool sicknessCounts = rules.quirks.on(quirk::SicknessAlwaysHits) || defender == PartnerId;
    if (view.partnerSick && sicknessCounts) {
      return Certain;
    }
    if (them.speedReserve <= 0 || isAttacking(*view.state, defender)) {
      return Certain;
    }
    if (move.finisher) {
      return {.percent = Hundred, .breaksBlock = them.blocking};
    }
    if (them.blocking) {
      return {.percent = 0, .breaksBlock = false};
    }
    std::int32_t evasion = move.accuracy / rules.hit.accuracyDivisor *
                           (them.stats.speed - (me.stats.speed / rules.hit.attackerSpeedDivisor)) /
                           rules.hit.speedScale;
    if (them.pose == Pose::Standing) {
      evasion = evasion * rules.hit.standingEvasionPercent / Hundred;
    }
    return {.percent = std::clamp(move.accuracy - evasion, 0, Hundred), .breaksBlock = false};
  }

  // battle_main.c: BTL_calculateDamage.
  std::int32_t damage(const BattleView& view, FighterId attacker, FighterId defender,
                      MoveRef ref) override {
    const DamageRules& rules = view.rules->damage;
    const Fighter& me = view.state->fighters.at(attacker);
    const Fighter& them = view.state->fighters.at(defender);
    const BattleMove& move = view.moves->at(ref);
    std::int32_t affinity = 0;
    for (const std::optional<std::int32_t>& special : them.combatant.specials) {
      affinity += special ? affinityOf(view.rules->affinity, move.special, *special)
                          : view.rules->affinity.emptySlot;
    }
    const std::int32_t offense = me.stats.offense;
    std::int32_t defense = them.stats.defense;
    if (move.counter) {
      defense = defense * rules.counterDefensePercent / Hundred;
    }
    std::int32_t damage = 0;
    if (move.finisher) {
      damage = (offense + move.power) * affinity / rules.affinityDivisor;
      if (attacker == PartnerId) {
        if (view.state->finisher.charge > rules.finisherChargeDivisor) {
          damage = damage * view.state->finisher.charge / rules.finisherChargeDivisor;
        }
      } else {
        damage = damage * roll(view, rules.enemyFinisherMin, rules.enemyFinisherMax) / Hundred;
      }
      damage = damage * roll(view, rules.varianceMin, rules.varianceMax) / Hundred;
    } else {
      const std::int32_t difference =
          std::clamp(offense - defense, -rules.differenceCap, rules.differenceCap);
      damage = affinity * (move.power + (difference * move.power / rules.powerScale)) /
               rules.affinityDivisor * roll(view, rules.varianceMin, rules.varianceMax) / Hundred;
    }
    return std::clamp(damage, rules.min, rules.max);
  }

private:
  static std::int32_t roll(const BattleView& view, std::int32_t low, std::int32_t high) {
    return view.random->below(high - low + 1) + low;
  }
};

} // namespace

std::unique_ptr<DamageFormula> makeOriginalDamage() {
  return std::make_unique<OriginalDamage>();
}

} // namespace dwe::game::battle
