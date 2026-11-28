// An engine addition: the original has no co-op (docs/architecture/coop-difficulty.md §5).
#include <dwe/game/battle/coop_scaling.hpp>

#include <algorithm>

namespace dwe::game::battle {

ScalingFactors coopScalingFactors(const BattleRules& rules, CoopLevel level, std::size_t partners,
                                  std::int32_t helpers, bool fixed) {
  helpers = std::max(helpers, 0);
  if (level == CoopLevel::Off || !rules.coopScaling || partners == 0 ||
      (partners == 1 && helpers == 0)) {
    return {};
  }
  const CoopScalingRules& scaling = *rules.coopScaling;
  const CoopScalingTable& table = fixed ? scaling.fixed : scaling.normal;
  const std::size_t column = std::min(partners, CoopScalingPartners) - 1;
  ScalingFactors factors{.hp = table.hpPercent.at(column),
                         .mp = table.mpPercent.at(column),
                         .offense = table.offensePercent.at(column),
                         .finisherFill = table.finisherFillPercent.at(column)};
  factors.hp += helpers * scaling.helperHpPercent;
  if (level == CoopLevel::Hard) {
    factors.hp += scaling.hardHpBonusPercent;
  }
  return factors;
}

} // namespace dwe::game::battle
