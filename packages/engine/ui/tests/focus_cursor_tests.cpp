#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

constexpr std::string_view Corners = "nb-ocur-outer";

bool drawsCorners(const Ui& ui, std::string_view document, std::string_view element) {
  return ui.styleProperty(document, element, "decorator").contains(Corners);
}

bool surrounds(const Ui::Rect& outer, const Ui::Rect& inner) {
  return outer.left < inner.left && outer.top < inner.top && outer.right > inner.right &&
         outer.bottom > inner.bottom;
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("the pixel skin draws the gold four corners round the battle's focused command") {
  fixture::Screen screen{"battle"};
  screen.skin("pixel");
  Ui& ui = *screen.ui;
  ui.setSetting("battle.phase", std::string{"fight"});
  ui.setSetting("battle.menu.count", 2.0);
  for (int index = 0; index < 2; ++index) {
    const std::string prefix = "battle.menu." + std::to_string(index) + ".";
    ui.setSetting(prefix + "command", index == 0 ? 1.0 : 3.0);
    ui.setSetting(prefix + "label", std::string{"battle.command.flee"});
    ui.setSetting(prefix + "icon", std::string{index == 0 ? "command-1" : "command-3"});
    ui.setSetting(prefix + "available", true);
    ui.setSetting(prefix + "current", index == 1);
    ui.setSetting(prefix + "mp_cost", 0.0);
  }
  ui.show("battle", true);
  screen.frames();
  std::ignore = ui.takeCommands();
  REQUIRE(ui.focusedElementId("battle") == "command-1");

  CHECK(drawsCorners(ui, "battle", "command-cursor"));
  const auto cursor = ui.elementRect("battle", "command-cursor");
  const auto tile = ui.elementRect("battle", "command-1");
  REQUIRE(cursor.has_value());
  REQUIRE(tile.has_value());
  CHECK(surrounds(*cursor, *tile));
}

TEST_CASE("the pixel skin draws the gold four corners round the schedule's focused cup") {
  fixture::Screen screen{"tournament_schedule"};
  screen.skin("pixel");
  Ui& ui = *screen.ui;
  ui.setSetting("tournament_schedule.phase", std::string{"schedule"});
  ui.setSetting("tournament_schedule.revision", 1.0);
  ui.show("tournament_schedule", true);
  screen.frames();

  CHECK(drawsCorners(ui, "tournament_schedule", "cursor"));
  const auto cursor = ui.elementRect("tournament_schedule", "cursor");
  const auto cell = ui.elementRect("tournament_schedule", "cell-0-0");
  REQUIRE(cursor.has_value());
  REQUIRE(cell.has_value());
  CHECK(surrounds(*cursor, *cell));
}

TEST_CASE("the pixel skin draws the gold four corners round the egg in focus only") {
  fixture::Screen screen{"egg_box"};
  screen.skin("pixel");
  Ui& ui = *screen.ui;
  ui.setSetting("egg_box_eggs", 4.0);
  ui.show("egg_box", true);
  screen.frames(20);
  REQUIRE(ui.focusedElementId("egg_box") == "egg-1");

  CHECK(drawsCorners(ui, "egg_box", "egg-1"));
  CHECK_FALSE(drawsCorners(ui, "egg_box", "egg-2"));
}

TEST_CASE("the pixel skin draws the gold four corners round the randomizer's focused preset") {
  fixture::Screen screen{"new_game_randomizer"};
  screen.skin("pixel");
  Ui& ui = *screen.ui;
  ui.show("new_game_randomizer", true);
  screen.frames(20);
  REQUIRE(ui.focusedElementId("new_game_randomizer") == "preset-off");

  CHECK(drawsCorners(ui, "new_game_randomizer", "preset-off"));
  CHECK_FALSE(drawsCorners(ui, "new_game_randomizer", "preset-chaos"));
  CHECK_FALSE(drawsCorners(ui, "new_game_randomizer", "evolutions"));
}

TEST_CASE("the pixel skin keeps its four-corner cursor in one shared rule") {
  const std::filesystem::path pixel = std::filesystem::path{DWE_GAME_PACK} / "ui/skins/pixel";
  for (const auto& entry : std::filesystem::recursive_directory_iterator{pixel}) {
    if (entry.path().extension() != ".rcss") {
      continue;
    }
    CAPTURE(entry.path().string());
    const bool shared = entry.path().filename() == "theme.rcss";
    CHECK(readFile(entry.path()).contains(Corners) == shared);
  }
}

} // namespace dwe::ui
