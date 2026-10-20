#pragma once

#include <string_view>

namespace dwe::game::field::messages {

// field/rules: a frame count or a shade of `transition` below 1 (the key's name).
inline constexpr std::string_view TransitionCountTooLow = "transition: {} must be at least 1";
// field/rules: a shade of `transition` beyond what a color channel holds (the limit).
inline constexpr std::string_view TransitionShadeTooHigh = "transition: shade_max and shade_full must be at most {}";

} // namespace dwe::game::field::messages
