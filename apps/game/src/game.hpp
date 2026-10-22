#pragma once

#include "options.hpp"

namespace dwe::app {

// The game: the field from the content's packs (and mods), with the HUD, the menu and the minimap,
// until the window closes or the menu quits. With --autotest it runs the route and returns its
// result.
int runGame(const Options& options);

} // namespace dwe::app
