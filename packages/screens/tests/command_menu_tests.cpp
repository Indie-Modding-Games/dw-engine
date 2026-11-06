#include <dwe/screens/command_menu.hpp>

#include <doctest/doctest.h>

#include <utility>
#include <vector>

namespace {

using dwe::screens::CommandMenuMotion;

std::pair<int, int> edges(const CommandMenuMotion& menu) {
  return {menu.top(), menu.bottom()};
}

} // namespace

TEST_CASE("the command strip drops in, bottom edge first, in 13 frames") {
  CommandMenuMotion menu;
  CHECK_FALSE(menu.shown());
  menu.open();
  CHECK(menu.shown());
  CHECK(edges(menu) == std::pair{-152, -120});
  // BTL_tickCommandMenu: BTL_COMMAND_MENU_SLIDE_Y for the bottom, then _X for the top; the 7th
  // frame moves both.
  const std::vector<std::pair<int, int>> expected{
      {-152, -106}, {-152, -94}, {-152, -84}, {-152, -77}, {-152, -72}, {-152, -69}, {-151, -68},
      {-148, -68},  {-143, -68}, {-136, -68}, {-126, -68}, {-114, -68}, {-100, -68}, {-100, -68},
  };
  for (const auto& edge : expected) {
    menu.step(false);
    CHECK(edges(menu) == edge);
  }
}

TEST_CASE("the hovered command blinks every 8 frames from the opening, only while fighting") {
  CommandMenuMotion menu;
  menu.open();
  // Measured: the banner lasts until the strip's timer reads 55, and the icon does not swap
  // meanwhile.
  for (int frame = 1; frame <= 55; ++frame) {
    menu.step(false);
    CHECK_FALSE(menu.blink());
  }
  CHECK(menu.frames() == 55);
  // The fight's first frame reads 56: the second frame shows at once, for 8 frames.
  menu.step(true);
  CHECK(menu.blink());
  for (int frame = 57; frame < 64; ++frame) {
    menu.step(true);
    CHECK(menu.blink());
  }
  menu.step(true);
  CHECK_FALSE(menu.blink());
  // After the fight it stays as it was.
  for (int frame = 0; frame < 8; ++frame) {
    menu.step(false);
  }
  CHECK_FALSE(menu.blink());
}

TEST_CASE("the command strip goes back up top edge first and the battle ends 16 frames on") {
  CommandMenuMotion menu;
  menu.open();
  for (int frame = 0; frame < 20; ++frame) {
    menu.step(true);
  }
  menu.close();
  // battle_main.c: while (BTL_isCommandMenuClosed() == 0) BTL_battleTickFrame();
  const std::vector<std::pair<int, int>> expected{
      {-114, -68}, {-126, -68}, {-136, -68}, {-143, -68}, {-148, -68},  {-151, -68},  {-152, -69},
      {-152, -72}, {-152, -77}, {-152, -84}, {-152, -94}, {-152, -106}, {-152, -120},
  };
  int frames = 0;
  while (!menu.closed()) {
    menu.step(false);
    if (frames < static_cast<int>(expected.size())) {
      CHECK(edges(menu) == expected.at(static_cast<std::size_t>(frames)));
    }
    ++frames;
    REQUIRE(frames < 40);
  }
  CHECK(frames == 16);
  CHECK_FALSE(menu.shown());
}

TEST_CASE("the command strip publishes its edges and blink") {
  CommandMenuMotion menu;
  menu.open();
  const dwe::screens::ScreenSettings settings = menu.settings();
  REQUIRE(settings.size() == 3);
  CHECK(settings.at(0).name == dwe::screens::CommandMenuTopSetting);
  CHECK(std::get<double>(settings.at(0).value) == -152.0);
  CHECK(std::get<double>(settings.at(1).value) == -120.0);
  CHECK_FALSE(std::get<bool>(settings.at(2).value));
}

TEST_CASE("a cup match's strip stands at the foot of the screen and leaves at once") {
  CommandMenuMotion menu;
  menu.openForMatch();
  // STD_addCommandMenu: STD_COMMAND_MENU_TOP 0x44, the bottom 0x20 under it, no slide.
  CHECK(edges(menu) == std::pair{68, 100});
  for (int frame = 1; frame <= 16; ++frame) {
    menu.step(true);
    CHECK(edges(menu) == std::pair{68, 100});
    // STD_tickCommandMenu still swaps the hovered icon every 8 frames.
    CHECK(menu.blink() == (frame >= 8 && frame < 16));
  }
  CHECK_FALSE(menu.closed());
  menu.close();
  // STD_removeCommandMenu: no slide back and no frames to wait.
  CHECK(menu.closed());
  CHECK_FALSE(menu.shown());
  // A field battle after it slides again.
  menu.open();
  CHECK(edges(menu) == std::pair{-152, -120});
}
