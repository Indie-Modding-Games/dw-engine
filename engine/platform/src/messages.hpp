#pragma once

#include <string_view>

// The error messages of platform. Templates take their arguments in the order of their placeholders.
namespace dwe::platform::messages {

inline constexpr std::string_view NoUserDataFolder = "cannot find the user's data folder: {}";

} // namespace dwe::platform::messages
