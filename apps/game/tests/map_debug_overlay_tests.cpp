#include <dwe/assets/map_document.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "map_debug_overlay.hpp"

namespace {

using dwe::app::bandColor;
using dwe::app::buildMapOverlay;
using dwe::app::CellBand;
using dwe::app::cellBandOf;
using dwe::assets::CollisionCell;
using dwe::assets::MapCollision;
using dwe::assets::MapDocument;
using dwe::debug::OverlayShapes;

// A camera straight above the grid that shows all of it: world x -5000..5000 left to right, z
// 5000..-5000 top to bottom.
std::optional<glm::vec2> above(glm::vec3 world) {
  return glm::vec2{(world.x + 5000.0F) / 10000.0F, (5000.0F - world.z) / 10000.0F};
}

bool hasLabel(const OverlayShapes& shapes, const std::string& text) {
  return std::ranges::any_of(shapes.labels, [&](const auto& label) { return label.text == text; });
}

TEST_CASE("collision values fall in the original's trigger bands") {
  CHECK(cellBandOf(0) == CellBand::Free);
  CHECK(cellBandOf(0x80) == CellBand::Wall);
  CHECK(cellBandOf(0x80 | 112) == CellBand::Wall);
  CHECK(cellBandOf(50) == CellBand::Other);
  CHECK(cellBandOf(51) == CellBand::Step);
  CHECK(cellBandOf(79) == CellBand::Step);
  CHECK(cellBandOf(80) == CellBand::Confirm);
  CHECK(cellBandOf(109) == CellBand::Confirm);
  CHECK(cellBandOf(110) == CellBand::Exit);
  CHECK(cellBandOf(119) == CellBand::Exit);
  CHECK(cellBandOf(120) == CellBand::Toilet);
  CHECK(cellBandOf(121) == CellBand::Other);
  CHECK(bandColor(CellBand::Free) == 0);
  CHECK(bandColor(CellBand::Step) != bandColor(CellBand::Confirm));
}

TEST_CASE("the overlay draws the walls, the trigger cells, the exits and the NPCs") {
  MapDocument map;
  map.id = "testa01";
  map.collision.setValue(CollisionCell{.column = 10, .row = 10}, 0x80);
  map.collision.setValue(CollisionCell{.column = 11, .row = 10}, 0x80);
  map.collision.setValue(CollisionCell{.column = 20, .row = 20}, 112);
  map.collision.setValue(CollisionCell{.column = 21, .row = 20}, 112);
  map.collision.setValue(CollisionCell{.column = 30, .row = 30}, 60);
  map.collision.setValue(CollisionCell{.column = 31, .row = 30}, 120);
  map.warps.push_back({.exit = 2,
                       .spawn = {100, 0, -200},
                       .rotation = 0,
                       .targetMap = 7,
                       .targetMapId = "testb02",
                       .targetExit = 3});
  dwe::assets::MapNpc npc;
  npc.scriptId = 6;
  npc.position = {300, 0, 400};
  npc.waypoints = {{400, 0, 400}, {500, 0, 500}};
  map.npcs = {dwe::assets::MapNpc{}, npc};

  OverlayShapes shapes;
  buildMapOverlay(map, map.collision, above, shapes);
  // Six cells: two walls, two exit cells, a step cell and the toilet.
  CHECK(shapes.quads.size() == 6);
  CHECK(std::ranges::count(shapes.quads, bandColor(CellBand::Wall),
                           &dwe::debug::OverlayQuad::fill) == 2);
  // A label per trigger value: the exit names where it leads.
  CHECK(hasLabel(shapes, "exit 2 -> testb02:3"));
  CHECK(hasLabel(shapes, "step 60"));
  CHECK(hasLabel(shapes, "toilet"));
  // Where the exit puts the player, and each NPC slot with its script id.
  CHECK(hasLabel(shapes, "in 2"));
  CHECK(hasLabel(shapes, "npc 0 id 0"));
  CHECK(hasLabel(shapes, "npc 1 id 6"));
  // The second NPC's two waypoints, joined to its place: two lines, plus a facing line each for
  // the exit's spawn and the two NPCs.
  CHECK(shapes.lines.size() == 2 + 3);

  // A camera that shows nothing of the grid draws no cells.
  OverlayShapes none;
  buildMapOverlay(
      map, map.collision, [](glm::vec3) { return std::optional<glm::vec2>{glm::vec2{5.0F}}; },
      none);
  CHECK(none.quads.empty());
}

} // namespace
