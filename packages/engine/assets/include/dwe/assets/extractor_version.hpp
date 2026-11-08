#pragma once

namespace dwe::assets {

// The version of what dwx extract writes from a disc (the base pack). dwx names its dev cache's
// entries with it, and the game reads the player's disc again when its installed content was
// written by another version (content::installIsOutdated). Bump it whenever the base pack written
// from the same disc changes: dwx's fingerprint test fails until it is bumped
// (docs/modulos/apps/dwx.md).
inline constexpr int ExtractorVersion = 13;

} // namespace dwe::assets
