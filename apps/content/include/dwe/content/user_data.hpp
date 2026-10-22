#pragma once

#include <string_view>

namespace dwe::content {

// Where the game and dwe-sim keep the user's own files (settings, saves, the dev cache of extracted
// discs): <user data>/<organization>/<application>/ (platform::userDataFolder).
inline constexpr std::string_view UserDataOrganization = "indie-modding-games";
inline constexpr std::string_view UserDataApplication = "dwe";

} // namespace dwe::content
