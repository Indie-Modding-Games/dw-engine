#pragma once

#include <string_view>

namespace dwe::app::messages {

// The log messages of the game's start, as std::format templates for core::formatMessage.
inline constexpr std::string_view RulesetFailed = "game: ruleset {}: {}";
inline constexpr std::string_view RulesetActive = "game: ruleset {} {} ({}), sections {}";
inline constexpr std::string_view RulesetSection = "{} ({})";
inline constexpr std::string_view ListSeparator = ", ";

// Autotest: a wait for a map change's name that has not come yet (the map id).
inline constexpr std::string_view MapNameNotShown = "no map change shows the name of {}";

} // namespace dwe::app::messages
