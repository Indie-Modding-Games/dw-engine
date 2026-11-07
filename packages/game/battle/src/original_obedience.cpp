#include <algorithm>

#include "original_strategies.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;

class OriginalObedience final : public Obedience {
public:
  // battle_main.c: BTL_tickPartnerAI and BTL_tickEnemyAI: below a brain of 301, every (brain / 2 +
  // 1) * 20 frames, a fighter that is not about to fall may start to ignore everything: the partner
  // with (70 - discipline) %, an enemy with (300 - brain) / 4 %. In a versus match both sides roll
  // as the partner, each with its own discipline (vs_combat.c: VS__tickDigimonAI).
  bool disobeys(const BattleView& view, FighterId fighter, std::int32_t discipline) override {
    const ObedienceRules& rules = view.rules->obedience;
    const Fighter& self = view.state->fighters.at(fighter);
    if (self.hp <= self.pendingDamage || self.stats.brain >= rules.brainBelow) {
      return false;
    }
    const std::int64_t period =
        static_cast<std::int64_t>((self.stats.brain / rules.rollBrainDivisor) + 1) *
        rules.rollPeriodFrames;
    if (view.state->frame % period != 0) {
      return false;
    }
    const std::int32_t chance =
        view.state->commanded(fighter)
            ? rules.partnerChanceBase - discipline
            : (rules.enemyChanceBrain - self.stats.brain) / rules.enemyChanceDivisor;
    return chance > view.random->below(Hundred);
  }

  // battle_tick.c: handleCommands: below 70 discipline, 160 - discipline / 10 frames; else
  // (10 - discipline / 10) * 10. The original then sets it to 0.
  std::int32_t commandDelay(const BattleView& view, std::int32_t discipline) override {
    const CommandDelayRules& rules = view.rules->obedience.commandDelay;
    if (view.rules->quirks.on(quirk::CommandDelayDiscarded)) {
      return 0;
    }
    if (discipline < rules.lowDisciplineBelow) {
      return rules.lowBase - (discipline / rules.lowDivisor);
    }
    return std::max(0, (rules.highBase - (discipline / rules.highDivisor)) * rules.highScale);
  }

  // battle_main.c: BTL_retargetAfterHit: TARGET_ATTACKER_CHANCE by charge mode, less the
  // discipline.
  bool partnerTurnsToAttacker(const BattleView& view, std::int32_t discipline) override {
    const auto mode = static_cast<std::size_t>(view.state->fighters.at(view.actor).chargeMode);
    return view.rules->obedience.retargetChances.at(mode) - discipline >
           view.random->below(Hundred);
  }

  // battle_main.c: BTL_selectFighterTarget: an enemy that only targets the partner never turns; the
  // others do 71 % of the times.
  bool enemyTurnsToAttacker(const BattleView& view, FighterId enemy) override {
    if (view.state->fighters.at(enemy).combatant.focusesPartner) {
      return false;
    }
    return view.random->below(Hundred) < view.rules->obedience.enemyFollowAttackerPercent;
  }
};

} // namespace

std::unique_ptr<Obedience> makeOriginalObedience() {
  return std::make_unique<OriginalObedience>();
}

} // namespace dwe::game::battle
