#pragma once

#include <string_view>

namespace dwe::game::story::messages {

// A trigger or a variable outside the state (the number, the last valid one).
inline constexpr std::string_view TriggerOutOfRange = "story: trigger {} is outside 0-{}";
inline constexpr std::string_view VariableOutOfRange = "story: variable {} is outside 0-{}";

// The saved story state.
inline constexpr std::string_view StoryStateText = "the story state";
// The triggers of a save are not their hex digits (the count wanted).
inline constexpr std::string_view TriggerDigitsWrong = "triggers must be {} hex digits";
// A saved variable outside a byte (the value).
inline constexpr std::string_view VariableNotAByte = "a variable must be 0-255, not {}";

} // namespace dwe::game::story::messages
