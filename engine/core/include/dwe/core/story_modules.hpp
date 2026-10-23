#pragma once

#include <string_view>

// The version of the story modules dwx writes (formats: the Lua emitter), shared with the session
// that loads them, so a game refuses content from another emitter when it starts instead of
// failing mid-play.
namespace dwe::core::story_modules {

// Raised whenever what the emitter writes changes (dwx's ExtractorVersion with it).
inline constexpr int Version = 3;
// The first line of every module: this prefix, the version and ": ".
inline constexpr std::string_view HeaderPrefix = "-- dwx lua ";

} // namespace dwe::core::story_modules
