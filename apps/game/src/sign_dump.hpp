#pragma once

#include "options.hpp"

namespace dwe::app {

// --dump-signs <map>[,<map>...] --shots <folder> [--lang <language>]: writes, without a window,
// each map's background and texture pages as the player's content has them, with a grid every 16
// pixels and the sign regions outlined, the list of its sprites (page, palette and texels), and the
// same images with the language's signs painted: what an overlay author needs to find a sign and
// check it (docs/guides/carteles.md). The images come from the player's own disc and never go into
// the repository.
int dumpSigns(const Options& options);

} // namespace dwe::app
