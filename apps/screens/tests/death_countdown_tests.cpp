#include <dwe/screens/death_countdown.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "game/battle/tests/fixture.hpp"

using dwe::screens::deathCountdownAnchorOf;
using dwe::screens::DeathCountdownLook;
using dwe::screens::deathCountdownLookOf;
using dwe::screens::WindowFraction;

// command_menu.c: BTL_initializeDeathCountdown, BTL_addDeathCountdown, BTL_tickDeathCountdown and
// BTL_renderDeathCountdown, measured in the original: on its first tick the badge is 40 x 40 at
// scale 0x1000 with palette 13 and the digit 16 x 16 at 0x1000; on tick 26 both are at 0x0b33 and
// turned 0x180000.

TEST_CASE("the countdown pops its digit, then shrinks and spins away, a second each") {
  CHECK_FALSE(deathCountdownLookOf(0).has_value());
  CHECK(deathCountdownLookOf(1) == DeathCountdownLook{.digit = 5});
  // Steps 2 to 4 grow the digit 0x199 a step, 5 to 7 bring it back.
  CHECK(deathCountdownLookOf(2)->digitScale == doctest::Approx((0x1000 + 0x199) / 4096.0));
  CHECK(deathCountdownLookOf(4)->digitScale == doctest::Approx((0x1000 + (3 * 0x199)) / 4096.0));
  CHECK(deathCountdownLookOf(5)->digitScale == doctest::Approx((0x1000 + (2 * 0x199)) / 4096.0));
  CHECK(deathCountdownLookOf(7)->digitScale == doctest::Approx(1.0));
  CHECK(deathCountdownLookOf(4)->badgeScale == doctest::Approx(1.0));
  // The badge moves to its next palette every third step up to step 20.
  CHECK(deathCountdownLookOf(2)->badgePalette == 0);
  CHECK(deathCountdownLookOf(3)->badgePalette == 1);
  CHECK(deathCountdownLookOf(6)->badgePalette == 2);
  CHECK(deathCountdownLookOf(9)->badgePalette == 0);
  CHECK(deathCountdownLookOf(20)->badgePalette == 0);
  // From step 21: 5 % smaller and 64 degrees more a step (tick 26: 0x0b33, 0x180000).
  const DeathCountdownLook spinning = *deathCountdownLookOf(26);
  CHECK(spinning.digit == 5);
  CHECK(spinning.badgeScale == doctest::Approx(0x0b33 / 4096.0));
  CHECK(spinning.digitScale == doctest::Approx(0x0b33 / 4096.0));
  CHECK(spinning.turn == doctest::Approx(0x180000 / 4096.0));
  // Tick 31 starts the next digit with a fresh cycle; the steps run on from there.
  CHECK(deathCountdownLookOf(30)->digit == 5);
  CHECK(deathCountdownLookOf(31) == DeathCountdownLook{.digit = 4});
  CHECK(deathCountdownLookOf(61)->turn == doctest::Approx(11 * 64.0));
  CHECK(deathCountdownLookOf(62) == DeathCountdownLook{.digit = 3});
  CHECK(deathCountdownLookOf(90)->digit == 3);
  CHECK(deathCountdownLookOf(91)->digit == 2);
  CHECK(deathCountdownLookOf(120)->digit == 2);
  CHECK(deathCountdownLookOf(121)->digit == 1);
  // Drawn for the last time on tick 150.
  CHECK(deathCountdownLookOf(150)->digit == 1);
  CHECK_FALSE(deathCountdownLookOf(151).has_value());
}

TEST_CASE("the countdown stands on the partner's node, kept near the screen's middle") {
  constexpr double Aspect = 4.0 / 3.0;
  const auto pixel = [](double x, double y) {
    return WindowFraction{.x = 0.5 + (x / 320.0), .y = 0.5 + (y / 240.0)};
  };
  // Measured: the partner's node 1 at (3, -56) as the countdown was added.
  const WindowFraction measured = deathCountdownAnchorOf(pixel(3, -56), Aspect);
  CHECK(measured.x == doctest::Approx(pixel(3, -56).x));
  CHECK(measured.y == doctest::Approx(pixel(3, -56).y));
  const WindowFraction held = deathCountdownAnchorOf(pixel(150, -120), Aspect);
  CHECK(held.x == doctest::Approx(pixel(140, 0).x));
  CHECK(held.y == doctest::Approx(pixel(0, -100).y));
  const WindowFraction low = deathCountdownAnchorOf(pixel(-150, 101), Aspect);
  CHECK(low.x == doctest::Approx(pixel(-140, 0).x));
  CHECK(low.y == doctest::Approx(pixel(0, 100).y));
}

TEST_CASE("the countdown's setting is empty while it does not show") {
  const auto text = [](const dwe::screens::ScreenSettings& settings) {
    REQUIRE(settings.size() == 1);
    CHECK(settings.front().name == dwe::screens::DeathCountdownSetting);
    return std::get<std::string>(settings.front().value);
  };
  CHECK(text(dwe::screens::settingsOf(std::nullopt, {})).empty());
  CHECK(text(dwe::screens::settingsOf(deathCountdownLookOf(26), {.x = 0.5, .y = 0.25})) ==
        "50.00 25.00 5 0.700 0.700 384.0 0");
}

TEST_CASE("the countdown shows from the partner's frame 20 on the ground until it is revived") {
  using namespace battle_test;
  Battle battle = startBattle(duel(evenStats(30, 20), evenStats(500, 400)), 9);
  CHECK_FALSE(dwe::screens::deathCountdownTimerOf(battle).has_value());
  battle.defeatPartner();
  const std::int32_t shownAt = battle.rules().ending.reviveCountdownShownAt;
  while (battle.state().deathFrames < shownAt - 1 && battle.state().frame < 2000) {
    battle.step();
  }
  REQUIRE(battle.state().partner().fainted);
  CHECK_FALSE(dwe::screens::deathCountdownTimerOf(battle).has_value());
  battle.step();
  CHECK(dwe::screens::deathCountdownTimerOf(battle) == std::optional<std::int32_t>{1});
  battle.step();
  CHECK(dwe::screens::deathCountdownTimerOf(battle) == std::optional<std::int32_t>{2});
  // A revive takes it away (item.c: handleRestore, BTL_removeDeathCountdown).
  CHECK(battle.revivePartner(10));
  CHECK_FALSE(dwe::screens::deathCountdownTimerOf(battle).has_value());
}
