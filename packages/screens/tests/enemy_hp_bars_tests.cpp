#include <dwe/screens/enemy_hp_bars.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

using dwe::screens::EnemyHpBar;
using dwe::screens::enemyHpBarShown;
using dwe::screens::EnemyOnScreen;
using dwe::screens::WindowFraction;

namespace {

// A 4:3 window shows the original's 320 x 240 screen: a pixel is 1/320 of its width, 1/240 of its
// height, from the middle.
constexpr double Aspect = 4.0 / 3.0;

WindowFraction pixel(double x, double y) {
  return {.x = 0.5 + (x / 320.0), .y = 0.5 + (y / 240.0)};
}

EnemyOnScreen enemyAt(double x, double y, std::int32_t hp = 400, std::int32_t maxHp = 400) {
  return {.enemy = 1, .node = pixel(x, y), .aspect = Aspect, .hp = hp, .maxHp = maxHp};
}

} // namespace

TEST_CASE("an enemy's HP bar hangs 20 pixels right of its node 1 and 26 over it, and the leader "
          "line ends at the node") {
  // The forest capture: the node at (-2, 9) from the middle, the bar's corner at (18, -17).
  const EnemyHpBar bar = dwe::screens::enemyHpBarOf(enemyAt(-2, 9));
  CHECK(bar.enemy == 1);
  CHECK(bar.anchor.x == doctest::Approx(pixel(-2, 9).x));
  CHECK(bar.anchor.y == doctest::Approx(pixel(-2, 9).y));
  CHECK(bar.x == 20);
  CHECK(bar.y == -26);
}

TEST_CASE("an enemy's HP bar stops at x 85 and y -100 of the screen") {
  // Node 66 or more right of the middle: the bar's corner stays at x 85.
  CHECK(dwe::screens::enemyHpBarOf(enemyAt(65, 0)).x == 20);
  CHECK(dwe::screens::enemyHpBarOf(enemyAt(66, 0)).x == 85 - 66);
  CHECK(dwe::screens::enemyHpBarOf(enemyAt(120, 0)).x == 85 - 120);
  // Node more than 74 over the middle: the bar's top stays at y -100.
  CHECK(dwe::screens::enemyHpBarOf(enemyAt(0, -74)).y == -26);
  CHECK(dwe::screens::enemyHpBarOf(enemyAt(0, -75)).y == -100 + 75);
  CHECK(dwe::screens::enemyHpBarOf(enemyAt(0, -110)).y == -100 + 110);
  // Nothing holds it on the left or at the bottom.
  CHECK(dwe::screens::enemyHpBarOf(enemyAt(-150, 110)).x == 20);
  CHECK(dwe::screens::enemyHpBarOf(enemyAt(-150, 110)).y == -26);
}

TEST_CASE("a wide window keeps the bar's stops in the original's pixels from the middle") {
  EnemyOnScreen wide = enemyAt(0, 0);
  wide.aspect = 16.0 / 9.0;
  wide.node = {.x = 0.5 + (70.0 / (240.0 * wide.aspect)), .y = 0.5};
  CHECK(dwe::screens::enemyHpBarOf(wide).x == 85 - 70);
}

TEST_CASE("the HP bar's fill is HP * 50 / max HP pixels, tinted by its width") {
  // Full: 50 pixels, green (red 0x80, green 0xff).
  EnemyHpBar bar = dwe::screens::enemyHpBarOf(enemyAt(0, 0, 400, 400));
  CHECK(bar.fill == 50);
  CHECK(bar.red == 128);
  CHECK(bar.green == 255);
  CHECK(bar.hp == 400);
  // The forest capture: 309 of 400 is 38 pixels, red (50 - 38) * 127 / 35 + 128.
  bar = dwe::screens::enemyHpBarOf(enemyAt(0, 0, 309, 400));
  CHECK(bar.fill == 38);
  CHECK(bar.red == 171);
  CHECK(bar.green == 255);
  // 15 pixels: yellow.
  bar = dwe::screens::enemyHpBarOf(enemyAt(0, 0, 120, 400));
  CHECK(bar.fill == 15);
  CHECK(bar.red == 255);
  CHECK(bar.green == 255);
  // Under 15, the green fades: 255 - (15 - 14) * 127 / 10.
  bar = dwe::screens::enemyHpBarOf(enemyAt(0, 0, 119, 400));
  CHECK(bar.fill == 14);
  CHECK(bar.red == 255);
  CHECK(bar.green == 243);
  bar = dwe::screens::enemyHpBarOf(enemyAt(0, 0, 40, 400));
  CHECK(bar.fill == 5);
  CHECK(bar.green == 255 - (10 * 127 / 10));
  // Under 5: red 0xff, green 0x80.
  bar = dwe::screens::enemyHpBarOf(enemyAt(0, 0, 39, 400));
  CHECK(bar.fill == 4);
  CHECK(bar.red == 255);
  CHECK(bar.green == 128);
  // A dead enemy shows an empty bar and 0.
  bar = dwe::screens::enemyHpBarOf(enemyAt(0, 0, 0, 400));
  CHECK(bar.fill == 0);
  CHECK(bar.hp == 0);
}

TEST_CASE("the original's bars: a wild enemy's a while after a hit, never a cup's") {
  CHECK(enemyHpBarShown({.always = false, .match = false, .recentlyHit = true, .standing = true}));
  CHECK_FALSE(
      enemyHpBarShown({.always = false, .match = false, .recentlyHit = false, .standing = true}));
  CHECK_FALSE(
      enemyHpBarShown({.always = false, .match = true, .recentlyHit = true, .standing = true}));
}

TEST_CASE("enemy life bars: every enemy still standing, cups too, and none once it falls") {
  CHECK(enemyHpBarShown({.always = true, .match = false, .recentlyHit = false, .standing = true}));
  CHECK(enemyHpBarShown({.always = true, .match = true, .recentlyHit = false, .standing = true}));
  CHECK_FALSE(
      enemyHpBarShown({.always = true, .match = false, .recentlyHit = true, .standing = false}));
}

TEST_CASE("the HP bars pack nine values each for the battle document") {
  const std::vector<EnemyHpBar> bars{
      dwe::screens::enemyHpBarOf(enemyAt(-2, 9, 309, 400)),
      {.enemy = 2,
       .anchor = {.x = 0.25, .y = 0.75},
       .x = 15,
       .y = 20,
       .fill = 4,
       .red = 255,
       .green = 128,
       .hp = 30},
  };
  const dwe::screens::ScreenSettings settings = dwe::screens::settingsOf(bars);
  REQUIRE(settings.size() == 1);
  CHECK(settings.front().name == dwe::screens::HpBarsSetting);
  CHECK(std::get<std::string>(settings.front().value) ==
        "1 49.4 53.8 20 -26 38 171 255 309 2 25.0 75.0 15 20 4 255 128 30 ");
  CHECK(std::get<std::string>(dwe::screens::settingsOf(std::vector<EnemyHpBar>{}).front().value)
            .empty());
}

TEST_CASE("std_main.c STD_initializePlayerMarker: the marks only in a match of one species") {
  using dwe::screens::marksShown;
  CHECK(marksShown(true, "agumon", "agumon"));
  CHECK_FALSE(marksShown(true, "agumon", "gabumon"));
  CHECK_FALSE(marksShown(false, "agumon", "agumon"));
  CHECK_FALSE(marksShown(true, "", ""));
  // STD_renderPlayerMarker: each fighter's anchor, in percent of the window.
  const auto settings = dwe::screens::settingsOf(std::vector<dwe::screens::PlayerMarker>{
      {.fighter = 0, .anchor = {.x = 0.25, .y = 0.5}},
      {.fighter = 1, .anchor = {.x = 0.7512, .y = 0.4}},
  });
  REQUIRE(settings.size() == 1);
  CHECK(settings.front().name == dwe::screens::MarkersSetting);
  CHECK(std::get<std::string>(settings.front().value) == "0 25.0 50.0 1 75.1 40.0 ");
}
