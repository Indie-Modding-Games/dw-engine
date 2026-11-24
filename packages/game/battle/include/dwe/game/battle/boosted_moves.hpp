#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace dwe::game::battle {

// The optional `boosted_moves` section of a ruleset (rules/original/boosted_moves.yaml,
// docs/modules/game/battle.md, «Boosted techniques»): the one technique each species is better at
// as a partner, and how much better.
inline constexpr std::string_view BoostedMovesSection = "boosted_moves";

struct BoostedMoveRules {
  // The boosted technique's power and MP cost, in percent of the table's.
  std::int32_t powerPercent = 100;
  std::int32_t mpCostPercent = 100;
  // Species id -> the id of its boosted technique.
  std::map<std::string, std::string, std::less<>> species;

  friend bool operator==(const BoostedMoveRules&, const BoostedMoveRules&) = default;
};

core::Result<BoostedMoveRules> readBoostedMoveRules(const ruleset::Section& section);

// The boosted power, rounded down.
[[nodiscard]] std::int32_t boostedPower(const BoostedMoveRules& rules, std::int32_t power);
// The boosted MP cost, rounded up: a technique that costs MP never becomes free.
[[nodiscard]] std::int32_t boostedMpCost(const BoostedMoveRules& rules, std::int32_t cost);

} // namespace dwe::game::battle
