#pragma once

#include <dwe/game/evolution/inputs.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dwe::game::evolution {

// The stats a requirement compares, in the original's order (EvoRequirements): HP and MP divided,
// then offense, defense, speed and brains.
inline constexpr std::size_t RequiredStatCount = 6;

// One stat of a requirement against the partner.
struct StatLine {
  // The minimum, FavouredStat on a rookie target's favoured stat, or NoRequirement.
  std::int32_t required = 0;
  // The partner's stat as the rules compare it (HP and MP divided).
  std::int32_t value = 0;
  // The partner's stat as it is, and what the compared one is divided by: a minimum of `required`
  // asks for required * divisor of the stat itself.
  std::int32_t stat = 0;
  std::int32_t divisor = 1;
  // A minimum stat: reached (always with NoRequirement). A favoured stat: it is the partner's
  // highest one.
  bool met = false;
};

// What can give the bonus point: any one of them is enough.
enum class BonusKind : std::uint8_t {
  From,
  Discipline,
  Happiness,
  Battles,
  LearnedMoves,
};

struct BonusLine {
  BonusKind kind = BonusKind::From;
  // The value asked for; for From, the form the partner must be.
  std::int32_t required = 0;
  // The partner's value; for From, its form.
  std::int32_t value = 0;
  // A maximum instead of a minimum (only battles, with BattlesAreMaximumFlag).
  bool isMaximum = false;
  bool met = false;
};

// What the requirement of a target asks and how the partner stands, group by group, as the scoring
// counts it (evolution.c: calculateRequirementScore): one point each for care mistakes, weight,
// stats and the bonus. It leaves out the preference for forms not raised yet, which depends on the
// best candidate of a check.
struct RequirementReport {
  Species target = 0;

  std::int32_t careMistakes = 0;
  bool careIsMaximum = false;
  std::int32_t careValue = 0;
  bool careMet = false;

  // The weight asked for (NoRequirement for none) and the weight that scores, from weightMin to
  // weightMax.
  std::int32_t weight = 0;
  std::int32_t weightMin = 0;
  std::int32_t weightMax = 0;
  std::int32_t weightValue = 0;
  bool weightMet = false;

  // A rookie target scores when the partner's highest stat is one of its favoured ones; the others
  // when every stat reaches its minimum.
  bool favouredStats = false;
  std::array<StatLine, RequiredStatCount> stats{};
  // The partner's highest stat (an index of stats), the one a favoured stats target looks at.
  std::size_t highestStat = 0;
  bool statsMet = false;

  // Only the bonuses the requirement asks for, in the original's order.
  std::vector<BonusLine> bonus;
  bool bonusMet = false;

  std::int32_t points = 0;
  std::int32_t passPoints = 0;

  [[nodiscard]] bool passes() const { return points >= passPoints; }
  // The groups a target can score, one point each.
  static constexpr std::int32_t Groups = 4;
};

// How the partner stands toward each target of its chart, for the evolution chart.
struct EvolutionOutlook {
  Species species = 0;
  Level level = Level::None;
  // The counter now and from how many hours the level checks; nothing for a level that never
  // evolves naturally.
  std::int32_t evolutionTimer = 0;
  std::optional<std::int32_t> checkHours;
  // A baby's in-training form, which needs nothing but the hours.
  std::optional<Species> fixedTarget;
  // The chart's targets that have a requirement, in the chart's order.
  std::vector<RequirementReport> targets;
  // The form the check would pick with the partner as it is, once the counter reaches checkHours
  // (the rookie's fallback included).
  std::optional<Species> next;
  // A rookie with no candidate becomes this form from fallbackHours of the counter.
  std::optional<Species> fallback;
  std::int32_t fallbackHours = 0;

  // Whether the counter has reached the level's hours: the next check is due.
  [[nodiscard]] bool due() const { return checkHours && evolutionTimer >= *checkHours; }
};

} // namespace dwe::game::evolution
