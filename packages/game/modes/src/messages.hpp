#pragma once

#include <string_view>

// The error and log messages of the game modes, as std::format templates for core::formatMessage.
namespace dwe::game::modes::messages {

inline constexpr std::string_view ModeText = "a game mode";
// The manifest's id is not its folder's (the id, the folder).
inline constexpr std::string_view ModeIdNotFolder = "the mode's id '{}' must be its folder's, '{}'";
inline constexpr std::string_view ModeBadId =
    "'{}' is not a mode id: a-z, 0-9 and '_', starting with a letter";
inline constexpr std::string_view ModeEmptyText = "the text is empty";
// `ruleset` is not among `rulesets` (the ruleset).
inline constexpr std::string_view ModeRulesetNotListed =
    "the ruleset '{}' must also be one of rulesets";
inline constexpr std::string_view ModeRulesetTwice = "the ruleset '{}' is listed twice";
// A mode names a ruleset no pack has (the mode, the ruleset).
inline constexpr std::string_view ModeNoRuleset = "mode '{}': no pack has the ruleset '{}'";
// A mode left out of the catalog (the mode, the reason).
inline constexpr std::string_view ModeSkipped = "modes: the mode '{}' is left out: {}";
inline constexpr std::string_view ModesFound = "modes: {} game modes ({})";

} // namespace dwe::game::modes::messages
