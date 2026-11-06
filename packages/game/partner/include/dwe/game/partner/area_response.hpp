#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/game/partner/effects.hpp>

#include <cstdint>

namespace dwe::game::partner {

// How a species feels about a map, from the map's two region lists and the species' favored region
// (map.c: loadMapSetup): the lists are read side by side, slot by slot, and the first slot where
// either list holds the favored region decides; the first list means the partner likes the area,
// the second that it dislikes it, and no match leaves it neutral. The original keeps a region as a
// signed byte and the favored region as an unsigned one, so only the low byte of each counts, and a
// region of 128 or more (a negative byte) never matches. The original works this out once, when the
// map loads.
[[nodiscard]] AreaResponse areaResponseOf(const assets::MapRegions& regions,
                                          std::int32_t favoredRegion);

} // namespace dwe::game::partner
