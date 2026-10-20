#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::debug::messages {

using core::formatMessage;

// The messages of the debug overlay's tuning panel, as std::format templates for core::formatMessage.
inline constexpr std::string_view NotANumber = "{}: '{}' is not a number";
inline constexpr std::string_view NotAFlag = "{}: '{}' must be true or false";
inline constexpr std::string_view NotAValue = "{} is a group, not a value";
inline constexpr std::string_view Selected = "ruleset {} selected";
inline constexpr std::string_view Tuned = "{}.{} = {}";
inline constexpr std::string_view Reloaded = "ruleset {} reloaded from its files";
inline constexpr std::string_view Saved = "section {} saved to {}";
inline constexpr std::string_view NoSection = "there is no section '{}'";
inline constexpr std::string_view TuningLog = "tuning: {}";

// The console.
inline constexpr std::string_view UnknownCommand = "unknown console command '{}'; known: {}";

} // namespace dwe::debug::messages
