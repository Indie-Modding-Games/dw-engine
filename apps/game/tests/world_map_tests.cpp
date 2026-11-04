#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/game/field/script_warps.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "world_map.hpp"

namespace {

using dwe::app::WorldMapExit;
using dwe::app::WorldMapKeys;
using dwe::app::WorldMapKeyStep;

constexpr std::string_view SmallLayout = R"(
canvas: [1000, 500]
regions:
  - {id: city, name: 43, places: [43, 44], at: [500, 250], radius: 40, colour: "#d9bf8a"}
  - {id: forest, name: 0, places: [0], at: [300, 250], radius: 50, colour: "#6f9a52"}
  - {id: caves, name: 12, places: [3, 12, 17], at: [300, 100], radius: 30, colour: "#9b8468"}
roads:
  - [city, forest]
  - [forest, caves]
)";

dwe::assets::MapTable smallTable() {
  dwe::assets::MapTable table;
  const auto add = [&table](std::string id, std::int32_t place) {
    dwe::assets::MapEntry entry;
    entry.id = std::move(id);
    entry.loadingName = place;
    table.maps.push_back(entry);
  };
  add("twna01", 43);
  add("room01", 44);
  add("mayo01", 0);
  add("mayo02", 0);
  add("tunn01", 12);
  add("tunn05", 17);
  add("gias01", 30);
  return table;
}

dwe::app::WorldMapLayout smallLayout() {
  auto layout = dwe::app::parseWorldMapLayout(SmallLayout);
  REQUIRE(layout.has_value());
  return *layout;
}

} // namespace

TEST_CASE("the island map's layout reads its areas and roads") {
  const dwe::app::WorldMapLayout layout = smallLayout();
  CHECK(layout.width == 1000.0F);
  CHECK(layout.height == 500.0F);
  REQUIRE(layout.regions.size() == 3);
  CHECK(layout.regions[2].id == "caves");
  CHECK(layout.regions[2].places == std::vector<std::int32_t>{3, 12, 17});
  CHECK(layout.regions[0].colour == 0xD9BF8AFFU);
  REQUIRE(layout.roads.size() == 2);
  CHECK(layout.roads[1].from == 1);
  CHECK(layout.roads[1].to == 2);
}

TEST_CASE("a broken layout says what is wrong") {
  CHECK_FALSE(dwe::app::parseWorldMapLayout("regions: []").has_value());
  // A place in two areas.
  CHECK_FALSE(dwe::app::parseWorldMapLayout(R"(
canvas: [10, 10]
regions:
  - {id: a, name: 0, places: [1], at: [1, 1], radius: 1, colour: "#000000"}
  - {id: b, name: 0, places: [1], at: [2, 2], radius: 1, colour: "#000000"}
)")
                  .has_value());
  // A road to an area that is not listed.
  CHECK_FALSE(dwe::app::parseWorldMapLayout(R"(
canvas: [10, 10]
regions:
  - {id: a, name: 0, places: [1], at: [1, 1], radius: 1, colour: "#000000"}
roads: [[a, nowhere]]
)")
                  .has_value());
  // A colour that is not #rrggbb.
  CHECK_FALSE(dwe::app::parseWorldMapLayout(R"(
canvas: [10, 10]
regions:
  - {id: a, name: 0, places: [1], at: [1, 1], radius: 1, colour: "green"}
)")
                  .has_value());
}

TEST_CASE("a map lies in the area that lists its name") {
  const dwe::app::WorldMapLayout layout = smallLayout();
  const dwe::assets::MapTable maps = smallTable();
  CHECK(layout.regionOfMap(maps, "twna01") == 0);
  CHECK(layout.regionOfMap(maps, "room01") == 0);
  CHECK(layout.regionOfMap(maps, "mayo02") == 1);
  // The Lava Cave's name lies in the caves.
  CHECK(layout.regionOfMap(maps, "tunn05") == 2);
  // A name no area lists, and a map the table does not have.
  CHECK_FALSE(layout.regionOfMap(maps, "gias01").has_value());
  CHECK_FALSE(layout.regionOfMap(maps, "nowhere").has_value());
}

TEST_CASE("the player knows the areas of the maps he has stood on and the one he is in") {
  const dwe::app::WorldMapLayout layout = smallLayout();
  const dwe::assets::MapTable maps = smallTable();
  const std::set<std::string, std::less<>> visited{"mayo01", "gias01"};
  CHECK(dwe::app::knownRegions(layout, maps, visited, 2) == std::vector<bool>{false, true, true});
  // An old save visited nothing: only where he stands is known.
  CHECK(dwe::app::knownRegions(layout, maps, {}, 0) == std::vector<bool>{true, false, false});
  CHECK(dwe::app::knownRegions(layout, maps, {}, std::nullopt) ==
        std::vector<bool>{false, false, false});
  const dwe::ui::WorldMapPicture picture = dwe::app::worldMapPicture(layout, {true, false, true});
  REQUIRE(picture.areas.size() == 3);
  CHECK_FALSE(picture.areas[1].known);
  CHECK(picture.areas[2].x == 300.0F);
  CHECK(picture.roads.size() == 2);
}

TEST_CASE("the marker stands on the centre of the player's area") {
  const dwe::app::WorldMapLayout layout = smallLayout();
  const auto marker = dwe::app::worldMapMarker(layout, 2);
  REQUIRE(marker.has_value());
  CHECK(marker->u == doctest::Approx(0.3));
  CHECK(marker->v == doctest::Approx(0.2));
  CHECK_FALSE(dwe::app::worldMapMarker(layout, std::nullopt).has_value());
  CHECK_FALSE(dwe::app::worldMapMarker(layout, 9).has_value());
}

TEST_CASE("the exits are the walk warps the map has a cell for and the scripts' steps") {
  dwe::assets::MapDocument map;
  const auto warp = [](std::int32_t exit, std::optional<std::string> target) {
    dwe::assets::MapWarp out;
    out.exit = exit;
    out.targetMapId = std::move(target);
    return out;
  };
  map.warps = {warp(0, "twna01"), warp(1, "tunn01"), warp(2, "gias01"), warp(3, std::nullopt)};
  // Cells for warps 0, 1 and 3; warp 2 has none and leads nowhere.
  map.collision.setValue({.column = 10, .row = 10}, 110);
  map.collision.setValue({.column = 11, .row = 10}, 111);
  map.collision.setValue({.column = 12, .row = 10}, 113);
  std::vector<dwe::game::field::ScriptWarp> scripts(3);
  scripts[0].map = "mayo01";
  scripts[0].via = dwe::game::field::WarpVia::Step;
  scripts[0].targetMap = "tunn05";
  scripts[1].map = "mayo01";
  scripts[1].via = dwe::game::field::WarpVia::Talk;
  scripts[1].targetMap = "gias01";
  scripts[2].map = "mayo02";
  scripts[2].via = dwe::game::field::WarpVia::Step;
  scripts[2].targetMap = "gias01";
  const std::vector<std::string> exits = dwe::app::exitMapsOf(map, "mayo01", scripts);
  CHECK(exits == std::vector<std::string>{"twna01", "tunn01", "tunn05"});

  // By place: the two maps of the caves are two places; the player's own place is left out.
  const dwe::app::WorldMapLayout layout = smallLayout();
  const dwe::assets::MapTable maps = smallTable();
  const std::vector<std::string> leads{"twna01", "mayo02", "tunn01", "tunn05", "room01"};
  CHECK(dwe::app::worldMapExits(layout, maps, leads, 0) ==
        std::vector<WorldMapExit>{{.place = 43, .region = 0},
                                  {.place = 12, .region = 2},
                                  {.place = 17, .region = 2},
                                  {.place = 44, .region = 0}});
}

TEST_CASE("M opens the island map where the player walks freely and M or cancel close it") {
  using enum WorldMapKeyStep;
  CHECK(dwe::app::worldMapKeyStep({.mapPressed = true, .canOpen = true}) == Open);
  CHECK(dwe::app::worldMapKeyStep({.mapPressed = true, .canOpen = false}) == None);
  CHECK(dwe::app::worldMapKeyStep({.mapPressed = true, .canOpen = true, .enabled = false}) == None);
  CHECK(dwe::app::worldMapKeyStep({.cancelPressed = true, .canOpen = true}) == None);
  CHECK(dwe::app::worldMapKeyStep({.mapPressed = true, .open = true}) == Close);
  CHECK(dwe::app::worldMapKeyStep({.cancelPressed = true, .open = true}) == Close);
  // Turning the option off never traps the player in the map.
  CHECK(dwe::app::worldMapKeyStep({.mapPressed = true, .open = true, .enabled = false}) == Close);
  CHECK(dwe::app::worldMapKeyStep(WorldMapKeys{.open = true}) == None);
}

TEST_CASE("the game pack's island layout reads and places every map name of the original once") {
  std::ifstream stream{std::filesystem::path{DWE_GAME_PACK} / "ui/world_map/layout.yaml"};
  REQUIRE(stream.good());
  const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  const auto layout = dwe::app::parseWorldMapLayout(text);
  REQUIRE(layout.has_value());
  CHECK(layout->regions.size() >= 20);
  // Some of the names the original shows on entering a map.
  for (const std::int32_t place : {0, 12, 17, 30, 43, 59, 67, 69}) {
    CAPTURE(place);
    CHECK(layout->regionOfPlace(place).has_value());
  }
}
