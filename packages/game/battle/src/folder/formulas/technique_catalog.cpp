#include <dwe/game/battle/folder/formulas.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "mechanics/movement/movement.hpp"

namespace dwe::game::battle::folder {

namespace {

constexpr std::int64_t Hundred = 100;
// The cost is summed in hundredths of a percent of an MP, so that every part stays exact until the
// final rounding up.
constexpr std::int64_t Exact = Hundred * Hundred;

TechniqueFunctions functionsOf(const BattleMove& move) {
  TechniqueFunctions functions = 0;
  const auto add = [&functions](TechniqueFunction function) {
    functions |= static_cast<TechniqueFunctions>(function);
  };
  if (move.power > 0) {
    add(TechniqueFunction::Offensive);
  }
  if (move.status != StatusEffect::None) {
    add(TechniqueFunction::Control);
  }
  if (move.buff) {
    add(TechniqueFunction::Support);
  }
  if (move.counter) {
    add(TechniqueFunction::Defensive);
  }
  return functions;
}

// costo-mp.md §6: how far from its target a technique works; an area around the user reaches as far
// as it spreads, a global one anywhere.
ReachClass reachOf(const MpCostRules& costs, const BattleMove& move, const Technique& technique) {
  if (move.range == MoveRange::Self || move.buff) {
    return ReachClass::Personal;
  }
  if (technique.area == AreaShape::Global) {
    return ReachClass::Long;
  }
  if (move.range == MoveRange::Melee && technique.area != AreaShape::AroundSelf) {
    return ReachClass::Short;
  }
  const std::int64_t pixels = technique.area == AreaShape::AroundSelf
                                  ? technique.areaPixels
                                  : integerSqrt(move.distanceSquared);
  if (pixels <= costs.shortReachUpTo) {
    return ReachClass::Short;
  }
  return pixels <= costs.mediumReachUpTo ? ReachClass::Medium : ReachClass::Long;
}

AreaClass areaClassOf(const MpCostRules& costs, AreaShape area, std::int32_t pixels) {
  if (area == AreaShape::Single) {
    return AreaClass::Single;
  }
  if (area == AreaShape::Global) {
    return AreaClass::Large;
  }
  if (pixels <= costs.smallAreaUpTo) {
    return AreaClass::Small;
  }
  return pixels <= costs.mediumAreaUpTo ? AreaClass::Medium : AreaClass::Large;
}

std::int64_t durationPercent(const MpCostRules& costs, EffectDuration duration) {
  return costs.durationPercents.at(static_cast<std::size_t>(duration));
}

// costo-mp.md §8.1: a boost by its strongest raise, on one stat or several.
std::int64_t buffCost(const MpCostRules& costs, const BuffPercents& buff) {
  const std::int32_t strongest = std::max({buff.offense, buff.defense, buff.speed});
  const int raised = static_cast<int>(buff.offense > 0) + static_cast<int>(buff.defense > 0) +
                     static_cast<int>(buff.speed > 0);
  if (raised == 0) {
    return 0;
  }
  const std::size_t tier = strongest <= costs.lightBuffUpTo      ? 0
                           : strongest <= costs.moderateBuffUpTo ? 1
                                                                 : 2;
  const std::int32_t cost =
      raised == 1 ? costs.singleBuffCosts.at(tier) : costs.severalBuffCosts.at(tier);
  return cost * durationPercent(costs, costs.buffDuration) * Hundred;
}

} // namespace

std::int32_t mpCostOf(const FolderRules& rules, const BattleMove& move, const Technique& technique,
                      EffectDuration effectDuration) {
  if (move.finisher) {
    return 0;
  }
  const MpCostRules& costs = rules.mpCosts;
  std::int64_t exact = static_cast<std::int64_t>(move.power) * costs.powerPercent * Hundred;
  if (move.status != StatusEffect::None) {
    const auto status = static_cast<std::size_t>(move.status) - 1;
    exact += costs.statusCosts.at(status) * static_cast<std::int64_t>(move.statusChance) *
             durationPercent(costs, effectDuration);
  }
  if (move.buff) {
    exact += buffCost(costs, *move.buff);
  }
  exact += (costs.reachCosts.at(static_cast<std::size_t>(technique.reach)) +
            costs.areaCosts.at(static_cast<std::size_t>(technique.areaClass))) *
           Exact;
  return static_cast<std::int32_t>((exact + Exact - 1) / Exact);
}

Technique techniqueOf(const FolderRules& rules, const MoveCatalog& moves, MoveRef ref) {
  const BattleMove& move = moves.at(ref);
  const BattleMove& base = moves.at(moves.find(move.id).value_or(ref));
  const auto entry = rules.catalog.find(base.id);
  const TechniqueEntry none;
  const TechniqueEntry& own = entry == rules.catalog.end() ? none : entry->second;
  Technique technique;
  technique.functions = own.functions.value_or(functionsOf(base));
  const bool area = base.range == MoveRange::Area;
  technique.area = own.area.value_or(area ? AreaShape::AroundSelf : AreaShape::Single);
  technique.areaPixels = own.areaPixels.value_or(
      area ? static_cast<std::int32_t>(integerSqrt(base.distanceSquared)) : 0);
  if (technique.area == AreaShape::Single || technique.area == AreaShape::Global) {
    technique.areaPixels = 0;
  }
  technique.friendlyFire = own.friendlyFire.value_or(
      technique.area != AreaShape::Single && technique.serves(TechniqueFunction::Offensive));
  technique.reach = reachOf(rules.mpCosts, base, technique);
  technique.areaClass = areaClassOf(rules.mpCosts, technique.area, technique.areaPixels);
  technique.mpCost = own.mpCost.value_or(
      mpCostOf(rules, base, technique, own.effectDuration.value_or(rules.mpCosts.statusDuration)));
  if (move.boosted && base.mpCost > 0 && technique.mpCost > 0) {
    technique.mpCost = std::max<std::int32_t>(
        1, static_cast<std::int32_t>(
               ((static_cast<std::int64_t>(technique.mpCost) * move.mpCost) + base.mpCost - 1) /
               base.mpCost));
  }
  return technique;
}

std::int32_t coverDistanceBonus(const FolderRules& rules, std::int64_t distance) {
  if (rules.coverFarPixels <= 0 || distance >= rules.coverFarPixels) {
    return rules.coverFarBonusPercent;
  }
  const std::int64_t near = rules.coverNearBonusPercent;
  const std::int64_t far = rules.coverFarBonusPercent;
  return static_cast<std::int32_t>(
      near + ((far - near) * std::max<std::int64_t>(distance, 0) / rules.coverFarPixels));
}

} // namespace dwe::game::battle::folder
