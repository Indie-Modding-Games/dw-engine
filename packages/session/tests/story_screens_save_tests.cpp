#include <dwe/game/save/sections.hpp>
#include <dwe/game/story/screen_history.hpp>
#include <dwe/session/story_scenes.hpp>

#include <doctest/doctest.h>

using dwe::game::story::ScreenArrival;
using dwe::game::story::ScreenPlace;
using dwe::session::StoryScenes;

TEST_CASE("a save keeps the screen and the previous screen a fled battle goes back to") {
  // main_menu.c: writeSavegame keeps CURRENT_SCREEN, CURRENT_EXIT, PREVIOUS_SCREEN and
  // PREVIOUS_EXIT; main.c puts them back on a load.
  StoryScenes saved{{}};
  dwe::game::save::SaveSections out;
  REQUIRE(saved.addSaveSections(out).has_value());
  saved.screens().start(204, 9);
  saved.screens().arrive({.screen = 3, .exit = 1, .walkedWarp = 2});
  dwe::game::save::SaveFile file;
  REQUIRE(out.write(file).has_value());

  StoryScenes loaded{{}};
  dwe::game::save::SaveSections in;
  REQUIRE(loaded.addSaveSections(in).has_value());
  REQUIRE(in.read(file).has_value());
  CHECK(loaded.screens().place() ==
        ScreenPlace{.screen = 3, .exit = 1, .previousScreen = 204, .previousExit = 2});
  // The field enters the loaded map at the saved place, with no exit: the place stays.
  loaded.screens().arrive({.screen = 3, .exit = std::nullopt, .walkedWarp = std::nullopt});
  CHECK(loaded.screens().place().previousScreen == 204);
}

TEST_CASE("a save from before the section starts the screens over on the map it loads on") {
  StoryScenes loaded{{}};
  loaded.screens().start(204, 9);
  dwe::game::save::SaveSections in;
  REQUIRE(loaded.addSaveSections(in).has_value());
  dwe::game::save::SaveFile file;
  REQUIRE(in.read(file).has_value());
  loaded.screens().arrive({.screen = 7, .exit = std::nullopt, .walkedWarp = std::nullopt});
  CHECK(loaded.screens().place() ==
        ScreenPlace{.screen = 7, .exit = 0, .previousScreen = 7, .previousExit = 0});
}
