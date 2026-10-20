#pragma once

#include <dwe/ruleset/game_clock.hpp>

#include <cstdint>

// The numbers the partner's rules share that are not rules: units and markers.
namespace dwe::game::partner::constants {

inline constexpr std::int32_t Percent = 100;
inline constexpr std::int32_t Tenths = 10;
inline constexpr auto HoursPerDay = static_cast<std::int32_t>(ruleset::HoursPerDay);
inline constexpr auto MinutesPerHour = static_cast<std::int32_t>(ruleset::MinutesPerHour);
// Ends the list of a species' meal hours (raise.yaml: hunger_times).
inline constexpr std::int32_t NoMealHour = -1;
// The urge timer when the partner does not need the toilet (poopingTimer).
inline constexpr std::int32_t NoUrge = -1;
// The hunger window before the first one (starvationTimer).
inline constexpr std::int32_t NoWindow = -1;

} // namespace dwe::game::partner::constants
