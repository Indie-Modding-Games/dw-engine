// The egg box after a death in the original look (doo2.c: DOO2_openEggBox, DOO2_renderEggIcons):
// the original's box at -84, -70, 168 x 80 from the screen's centre with the four egg icons in a
// row, 36 pixels apart from x -54 at y -30, and the cursor round the egg in focus. One pixel of the
// original is 3 of the test window's 720 lines.

#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

void openEggBox(Screen& screen) {
  screen.ui->setSetting("egg_box_eggs", 4.0);
  screen.ui->show("egg_box", true);
  // The box grows for 5 frames at 20 a second.
  screen.frames(20);
}

} // namespace

TEST_CASE("the original egg box stands where the original's does, its eggs in a row") {
  Screen screen{"egg_box"};
  screen.skin("original");
  openEggBox(screen);
  const auto panel = screen.ui->elementRect("egg_box", "panel");
  REQUIRE(panel.has_value());
  // x 76 to 244 and y 50 to 130 of the original's 320 x 240, centred in 1280 x 720.
  CHECK(*panel == Ui::Rect{.left = 388.0F, .top = 150.0F, .right = 892.0F, .bottom = 390.0F});
  for (int egg = 0; egg < 4; ++egg) {
    CAPTURE(egg);
    const auto cell = screen.ui->elementRect("egg_box", "egg-" + std::to_string(egg + 1));
    REQUIRE(cell.has_value());
    // The 40 x 40 cursor's cell, centred on its icon at x -54 + 36 * egg and y -30.
    CHECK((cell->left + cell->right) / 2.0F == doctest::Approx(640.0 + 3.0 * (-54 + 36 * egg)));
    CHECK((cell->top + cell->bottom) / 2.0F == doctest::Approx(360.0 - 3.0 * 30));
    CHECK(cell->right - cell->left == doctest::Approx(120.0));
  }
}

TEST_CASE("the egg box's keys sound nothing of the menus': the game plays its own sounds") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"egg_box"};
    screen.skin(skin);
    openEggBox(screen);
    static_cast<void>(screen.ui->takeCues());
    screen.press(input::keys::Right);
    screen.press(input::keys::Left);
    screen.press(input::keys::Escape);
    CHECK(screen.ui->takeCues().empty());
  }
}

} // namespace dwe::ui
