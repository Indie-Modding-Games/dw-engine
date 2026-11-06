#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/debug/overlay_shapes.hpp>
#include <dwe/game/field/trigger_bands.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace dwe::app {

// The field map's debug overlay (docs/guides/control-remoto.md, «Capa de depuración del mapa»):
// over the real field view it draws the collision grid (walls, and the trigger cells coloured by
// their band), the exits with their index and target, where each exit puts the player, and the
// map's NPC slots with their waypoints. F3, the console's map_overlay("on"|"off"|"toggle") and
// the control channel's overlay turn it on and off.

// The console's map_overlay("on"|"off"|"toggle").
inline constexpr std::string_view MapOverlayCommand = "map_overlay";

// What a collision value does, by the field rules' trigger bands (field/triggers; the numbers are
// the original's defaults, map_collision.c).
enum class CellBand : std::uint8_t {
  Free,    // 0
  Wall,    // the blocked bit, 0x80
  Step,    // 51-79: a section of the map's script runs on stepping on it
  Confirm, // 80-109: a section runs on pressing confirm on it
  Exit,    // 110-119: the map's warp of exit value - 110
  Toilet,  // 120
  Other,   // any other trigger
};

CellBand cellBandOf(std::uint8_t value, const game::field::TriggerBands& bands = {});

// The colour a band's cells are filled with.
debug::OverlayColor bandColor(CellBand band);

// Where a world point falls on the window, as a fraction of it; nothing when it is behind the
// camera.
using OverlayProjector = std::function<std::optional<glm::vec2>(glm::vec3)>;

// Adds the overlay of `map` (its exits and NPCs) and of `grid` (the collision the field uses now,
// with the scripts' changes) to `out`, the cells classified by `bands` (the field rules'). Cells
// all of whose corners fall outside the window are left out.
void buildMapOverlay(const assets::MapDocument& map, const assets::MapCollision& grid,
                     const game::field::TriggerBands& bands, const OverlayProjector& project,
                     debug::OverlayShapes& out);

} // namespace dwe::app
