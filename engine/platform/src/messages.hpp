#pragma once

#include <string_view>

// The error messages of platform. Templates take their arguments in the order of their placeholders.
namespace dwe::platform::messages {

inline constexpr std::string_view NoUserDataFolder = "cannot find the user's data folder: {}";
inline constexpr std::string_view NoExecutableFolder = "cannot find the executable's folder: {}";
inline constexpr std::string_view NoProgram = "no program to run";
inline constexpr std::string_view CannotStartProcess = "cannot start {}: {}";
inline constexpr std::string_view CannotReadProcess = "cannot read the output of {}: {}";

} // namespace dwe::platform::messages
