#pragma once

#include <string_view>

// The version of the story modules dwx writes (formats: the Lua emitter), shared with the session
// that loads them, so a game refuses content from another emitter when it starts instead of
// failing mid-play.
namespace dwe::core::story_modules {

// Raised whenever what the emitter writes changes (dwx's ExtractorVersion with it).
// 4: the cancel arm of a selection without its end marker asks story.cancel_closes_choice().
inline constexpr int Version = 4;
// The oldest version the session still loads: what changed since only adds calls an older module
// lacks, and it keeps the original's behaviour without them.
inline constexpr int OldestReadable = 3;
// The first line of every module: this prefix, the version and ": ".
inline constexpr std::string_view HeaderPrefix = "-- dwx lua ";

} // namespace dwe::core::story_modules
