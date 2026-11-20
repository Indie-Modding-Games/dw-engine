#pragma once

#include <dwe/game/battle/rules.hpp>

#include <cstddef>
#include <cstdint>

namespace dwe::game::battle {

// The co-op difficulty the host chose (docs/architecture/coop-difficulty.md §9): off keeps the
// original numbers, standard follows the ruleset's tables, hard adds their HP bonus. An engine
// addition: the original has no co-op.
enum class CoopLevel : std::uint8_t { Off, Standard, Hard };

// How a co-op battle scales its enemies: the level and the players there whose partner does not
// fight (they help with items and cheers). The partners that fight the battle counts itself.
struct CoopScaling {
  CoopLevel level = CoopLevel::Off;
  std::int32_t helpers = 0;

  friend bool operator==(const CoopScaling&, const CoopScaling&) = default;
};

// Percents of the enemies' original values.
struct ScalingFactors {
  std::int32_t hp = 100;
  std::int32_t mp = 100;
  std::int32_t offense = 100;
  std::int32_t finisherFill = 100;

  friend bool operator==(const ScalingFactors&, const ScalingFactors&) = default;
};

// The factors for `partners` fighting partners and `helpers` helpers, in a normal or a fixed fight
// (trigger 1). One partner and no helper, the level off or a ruleset without coop_scaling: always
// 100 (single player never changes). Counts past the tables take their last column.
[[nodiscard]] ScalingFactors coopScalingFactors(const BattleRules& rules, CoopLevel level,
                                                std::size_t partners, std::int32_t helpers,
                                                bool fixed);

} // namespace dwe::game::battle
