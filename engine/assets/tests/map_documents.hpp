#pragma once

#include <dwe/assets/map_document.hpp>

#include <format>
#include <string>
#include <string_view>

// Valid documents of the map readers, for the tests of the engine and of dwx validate.
namespace dwe::assets::testing {

// The collision rows of the fixture: blocked border, a trigger 'A' (110) at column 10 of row 20 and
// a trigger 'B' (51) at column 11 of row 20.
inline std::string fixtureCollision() {
  std::string text;
  for (int row = 0; row < MapCollision::Size; ++row) {
    std::string cells(MapCollision::Size, '.');
    cells.front() = '#';
    cells.back() = '#';
    if (row == 0 || row == MapCollision::Size - 1) {
      cells.assign(MapCollision::Size, '#');
    }
    if (row == 20) {
      cells[10] = 'A';
      cells[11] = 'B';
    }
    text += std::format("  - \"{}\"\n", cells);
  }
  return text;
}

inline std::string fixtureWarps() {
  std::string text;
  for (int exit = 0; exit < 10; ++exit) {
    if (exit == 0) {
      text += "- exit: 0\n  spawn:\n    x: -2400\n    y: 0\n    z: -2425\n  rotation: 3072\n"
              "  target_map: 118\n  target_map_id: \"room08\"\n  target_exit: 2\n";
    } else {
      text += std::format("- exit: {}\n  spawn:\n    x: 0\n    y: 0\n    z: 0\n  rotation: 0\n"
                          "  target_map: -1\n  target_map_id: null\n  target_exit: -1\n",
                          exit);
    }
  }
  return text;
}

inline std::string fixtureMap(std::string_view objects = "null",
                              std::string_view extraNpcs = "[]") {
  return std::format(R"(camera:
  viewpoint:
    x: 0
    y: -4200
    z: -5900
  reference:
    x: 0
    y: 0
    z: -250
  projection: 1800
lights:
- direction: {{x: 0, y: 160, z: 90}}
  color: {{r: 170, g: 170, b: 170}}
- direction: {{x: 1, y: 1, z: 1}}
  color: {{r: 0, g: 0, b: 0}}
- direction: {{x: 1, y: 1, z: 1}}
  color: {{r: 0, g: 0, b: 0}}
ambient: {{r: 0, g: 0, b: 0}}
regions:
  a: [-1, -1, -1, -1]
  b: [1, 2, 3, 4]
tiles:
  width: 3
  height: 2
  rows:
  - "0 1 -1"
  - "2 3 4"
images:
- bits_per_pixel: 8
  offset: 300
  size: 66000
objects: {}
warps:
{}digimon_count: 1
npcs: {}
collision:
  width: 100
  height: 100
  encoding: "legend"
  legend:
  - cell: "."
    value: 0
  - cell: "A"
    value: 110
  - cell: "B"
    value: 51
  - cell: "#"
    value: 128
  rows:
{})",
                     objects, fixtureWarps(), extraNpcs, fixtureCollision());
}

// A map table with three maps, a door set and two toilets.
inline constexpr std::string_view FixtureTable = R"(maps:
- id: "twna01"
  index: 104
  filename: "TWNA01"
  images_8bpp: 3
  images_4bpp: 1
  flags: 137
  sound_id: 9
  fixed_time_of_day: false
  has_npcs: true
  doors: null
  toilet: 1
  loading_name: 2
- id: "room08"
  index: 118
  filename: "ROOM08"
  images_8bpp: 2
  images_4bpp: 1
  flags: 199
  sound_id: 7
  fixed_time_of_day: true
  has_npcs: true
  doors: 0
  toilet: null
  loading_name: 1
- id: "map_119"
  index: 119
  filename: ""
  images_8bpp: 0
  images_4bpp: 0
  flags: 0
  sound_id: 0
  fixed_time_of_day: false
  has_npcs: false
  doors: null
  toilet: null
  loading_name: 7
doors:
- index: 0
  doors:
  - {model_id: null, position: {x: 0, y: 0, z: 0}, rotation: 0}
  - {model_id: 4, position: {x: 1800, y: 0, z: -900}, rotation: 1024}
  - {model_id: null, position: {x: 0, y: 0, z: 0}, rotation: 0}
  - {model_id: null, position: {x: 0, y: 0, z: 0}, rotation: 0}
  - {model_id: null, position: {x: 0, y: 0, z: 0}, rotation: 0}
  - {model_id: 5, position: {x: -1800, y: 0, z: -500}, rotation: -1024}
toilets:
- {index: 0, x1: -200, z1: -1800, x2: -400, z2: -1000}
- {index: 1, x1: -2800, z1: -900, x2: -3300, z2: -700}
map_names:
- "       Test Woods       "
- "       Test Cabin       "
- "        Test Town       "
light_updates:
- {map: "twnb04", mode: 1, start_id: 23, count: 3, trigger: null}
- {map: 300, mode: 0, start_id: 1, count: 1, trigger: 7}
)";

} // namespace dwe::assets::testing
