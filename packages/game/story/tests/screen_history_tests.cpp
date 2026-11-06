#include <dwe/game/story/screen_history.hpp>

#include <doctest/doctest.h>

#include <optional>

using dwe::game::story::PreviousScreenOverride;
using dwe::game::story::ScreenArrival;
using dwe::game::story::ScreenHistory;
using dwe::game::story::ScreenPlace;

TEST_CASE("the previous screen follows the warps walked and the script map changes") {
  // map.c: changeMap keeps the screen left; tamer.c: checkMapInteraction keeps the warp walked
  // through as the previous exit, map.c: scriptTickChangeMap the exit the player is at.
  ScreenHistory screens;
  screens.start(204, 9);
  screens.arrive({.screen = 204, .exit = 9, .walkedWarp = std::nullopt});
  CHECK(screens.place() ==
        ScreenPlace{.screen = 204, .exit = 9, .previousScreen = 204, .previousExit = 9});
  screens.arrive({.screen = 3, .exit = 1, .walkedWarp = 2});
  CHECK(screens.place() ==
        ScreenPlace{.screen = 3, .exit = 1, .previousScreen = 204, .previousExit = 2});
  screens.arrive({.screen = 7, .exit = 4, .walkedWarp = std::nullopt});
  CHECK(screens.place() ==
        ScreenPlace{.screen = 7, .exit = 4, .previousScreen = 3, .previousExit = 1});
}

TEST_CASE("the first map entered with no place kept is the previous screen too") {
  // A start on a chosen map, or a save from before the place was saved: never the screen 0.
  ScreenHistory screens;
  screens.arrive({.screen = 18, .exit = 2, .walkedWarp = std::nullopt});
  CHECK(screens.place() ==
        ScreenPlace{.screen = 18, .exit = 2, .previousScreen = 18, .previousExit = 2});
  screens.forget();
  screens.arrive({.screen = 21, .exit = std::nullopt, .walkedWarp = std::nullopt});
  CHECK(screens.place() ==
        ScreenPlace{.screen = 21, .exit = 0, .previousScreen = 21, .previousExit = 0});
}

TEST_CASE(
    "an arrival with no exit keeps a place put back on that screen and starts over elsewhere") {
  ScreenHistory screens;
  screens.restore(ScreenPlace{.screen = 12, .exit = 3, .previousScreen = 11, .previousExit = 0});
  screens.arrive({.screen = 12, .exit = std::nullopt, .walkedWarp = std::nullopt});
  CHECK(screens.place() ==
        ScreenPlace{.screen = 12, .exit = 3, .previousScreen = 11, .previousExit = 0});
  screens.arrive({.screen = 40, .exit = std::nullopt, .walkedWarp = std::nullopt});
  CHECK(screens.place() ==
        ScreenPlace{.screen = 40, .exit = 0, .previousScreen = 40, .previousExit = 0});
}

TEST_CASE("a fled battle goes back to the screen and the warp the player came by") {
  // script_interp.c: start_battle fled: changeMap(PREVIOUS_SCREEN, PREVIOUS_EXIT), and the
  // previous exit becomes the exit the player was at.
  ScreenHistory screens{
      {.overrides = {PreviousScreenOverride{
           .from = std::nullopt, .to = 50, .previous = 49, .previousExit = std::nullopt}}}};
  screens.start(204, 9);
  screens.arrive({.screen = 3, .exit = 1, .walkedWarp = 2});
  const ScreenPlace back = screens.place();
  CHECK(back.previousScreen == 204);
  CHECK(back.previousExit == 2);
  screens.leaveByFlight();
  screens.arrive(
      {.screen = back.previousScreen, .exit = back.previousExit, .walkedWarp = std::nullopt});
  CHECK(screens.place() ==
        ScreenPlace{.screen = 204, .exit = 2, .previousScreen = 3, .previousExit = 1});
  // A special case of changeMap still names its own previous screen.
  screens.arrive({.screen = 50, .exit = 0, .walkedWarp = std::nullopt});
  CHECK(screens.place().previousScreen == 49);
}
