#pragma once

#include <string_view>

namespace dwe::app::messages {

// The log messages of the game's start, as std::format templates for core::formatMessage.
inline constexpr std::string_view RulesetFailed = "game: ruleset {}: {}";
inline constexpr std::string_view RulesetActive = "game: ruleset {} {} ({}), sections {}";
inline constexpr std::string_view RulesetSection = "{} ({})";
inline constexpr std::string_view ListSeparator = ", ";

} // namespace dwe::app::messages
