#include <dwe/game/partner/need_gauges.hpp>

#include <algorithm>
#include <cstddef>

#include "partner_constants.hpp"

namespace dwe::game::partner {
namespace {

constexpr std::int32_t MinutesPerDay = constants::HoursPerDay * constants::MinutesPerHour;

NeedGauge gaugeOf(std::int32_t left, std::int32_t span, bool due) {
  const std::int32_t max = std::max(span, 1);
  if (due) {
    return {.value = max, .max = max};
  }
  return {.value = max - std::clamp(left, 0, max), .max = max};
}

} // namespace

std::int32_t mealIntervalMinutes(const SpeciesCare& species, const HungerRules& rules,
                                 std::int32_t nextHungerHour) {
  if (species.level == Level::Baby) {
    return rules.babyIntervalHours * constants::MinutesPerHour;
  }
  if (species.level == Level::InTraining) {
    return rules.inTrainingIntervalHours * constants::MinutesPerHour;
  }
  const auto& hours = species.hungerHours;
  const auto end = std::ranges::find(hours, constants::NoMealHour);
  const auto count = static_cast<std::size_t>(end - hours.begin());
  if (count <= 1) {
    return MinutesPerDay;
  }
  const auto next = std::ranges::find(hours.begin(), end, nextHungerHour);
  const std::size_t at = next == end ? 0 : static_cast<std::size_t>(next - hours.begin());
  const std::int32_t previous = hours.at((at + count - 1) % count);
  const std::int32_t gap =
      (hours.at(at) - previous + constants::HoursPerDay) % constants::HoursPerDay;
  return (gap == 0 ? constants::HoursPerDay : gap) * constants::MinutesPerHour;
}

NeedGauges needGaugesOf(const PartnerState& state, const SpeciesCare& species,
                        const CareRules& rules) {
  const std::int32_t interval = species.poopInterval;
  const std::int32_t longestWait =
      rules.quirks.toiletDoublesPoopInterval ? interval + interval : interval;
  return {
      .hunger = gaugeOf(state.hunger.foodLevel,
                        mealIntervalMinutes(species, rules.hunger, state.hunger.nextHungerHour),
                        state.conditions.has(Condition::Hungry)),
      .toilet = gaugeOf(state.toilet.poopLevel, longestWait,
                        state.conditions.has(Condition::NeedsToilet)),
  };
}

} // namespace dwe::game::partner
