#pragma once

#include <string_view>

// The messages of the story's textboxes in the session, as std::format templates for
// messages::formatMessage.
namespace dwe::session::dialogue_messages {

// What the ruleset's dialogue section is called in its errors.
inline constexpr std::string_view SectionText = "the dialogue section";
// A rule below its least value (the key, the least value).
inline constexpr std::string_view RuleTooLow = "{} must be at least {}";
// Publishing the textbox orders failed (the reason).
inline constexpr std::string_view PublishFailed = "story: the textbox orders: {}";

inline constexpr std::string_view PortFreeze =
    "[port:dialogue] scriptPauseGame (script_interp.c:2727): speaker {}, runner {}";

} // namespace dwe::session::dialogue_messages
