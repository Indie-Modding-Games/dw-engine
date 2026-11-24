#pragma once

#include <dwe/game/partner/care_rules.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/game/partner/state.hpp>

#include <cstdint>

namespace dwe::game::partner {

// How far one need has built up: 0 right after it was met, `max` when it is due.
struct NeedGauge {
  std::int32_t value = 0;
  std::int32_t max = 0;

  friend bool operator==(const NeedGauge&, const NeedGauge&) = default;
};

// The needs that build up over time, read from the timers the care rules run (an engine
// improvement, the need meters; no rule reads it):
//   - hunger: the minutes of the meal gap gone by (foodLevel counts down to the next meal hour);
//   - toilet: the steps gone by since it last went (poopLevel counts down to the next need), over
//     the longest wait the rules give.
// A need the partner has now reads full. Tiredness is its own value (MoodState::tiredness).
struct NeedGauges {
  NeedGauge hunger;
  NeedGauge toilet;

  friend bool operator==(const NeedGauges&, const NeedGauges&) = default;
};

// The minutes between the meal before the next one and the next one (nextHungerHour): the
// interval hours of a baby or an in-training partner, else the gap in the species' meal hours
// (partner_impl.c: setFoodTimer), a whole day with a single meal.
[[nodiscard]] std::int32_t mealIntervalMinutes(const SpeciesCare& species, const HungerRules& rules,
                                               std::int32_t nextHungerHour);

[[nodiscard]] NeedGauges needGaugesOf(const PartnerState& state, const SpeciesCare& species,
                                      const CareRules& rules);

} // namespace dwe::game::partner
