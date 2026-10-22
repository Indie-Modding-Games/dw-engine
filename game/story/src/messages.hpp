#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::game::story::messages {

using core::formatMessage;

// The error and log messages of the story runtime, as std::format templates for core::formatMessage.

// A trigger or a variable outside the state (the number, the last valid one).
inline constexpr std::string_view TriggerOutOfRange = "story: trigger {} is outside 0-{}";
inline constexpr std::string_view VariableOutOfRange = "story: variable {} is outside 0-{}";

// The saved story state.
inline constexpr std::string_view StoryStateText = "the story state";
// The triggers of a save are not their hex digits (the count wanted).
inline constexpr std::string_view TriggerDigitsWrong = "triggers must be {} hex digits";
// A saved variable outside a byte (the value).
inline constexpr std::string_view VariableNotAByte = "a variable must be 0-255, not {}";

// The orders.
inline constexpr std::string_view NoSuchTrigger = "trigger {} is outside the story's {} triggers";
inline constexpr std::string_view NoSuchVariable = "variable {} is outside the story's {} variables";

// The runner.
inline constexpr std::string_view NoTicksPerFrame = "a frame of the original must last at least one tick";
inline constexpr std::string_view RequestDropped = "story: section {:#x} of script {} dropped: script {} is running";
inline constexpr std::string_view SectionFailedLog = "story: script {} section {:#x} failed: {}";
inline constexpr std::string_view SectionMissing = "story: script {} has no section {:#x}: {}";
inline constexpr std::string_view TooDeep =
    "story: script {} section {:#x} jumps from {} subroutine calls deep, past the guard of {}";
inline constexpr std::string_view ReturnWithNothing =
    "story: script {} section {:#x} returns with nothing to return to";
inline constexpr std::string_view NeverWaits = "story: script {} section {:#x} resumed {} times in one frame";

} // namespace dwe::game::story::messages
