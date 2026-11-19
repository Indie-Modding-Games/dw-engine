#include <dwe/screens/finisher_gauge.hpp>

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <variant>

using dwe::screens::finisherGaugeCornerOf;
using dwe::screens::finisherGaugeSettingsOf;
using dwe::screens::WindowFraction;

// command_menu.c: BTL_initializeFinisherChargeup, measured in the original (11_battle_agumon):
// the partner's point one pixel right of the middle gives BTL_FINISHER_CHARGEUP_POS (-22, -89).
TEST_CASE("the charge gauge hangs 23 pixels left and 20 up of the point over the partner") {
  constexpr double Aspect = 4.0 / 3.0;
  const WindowFraction point{.x = 0.5 + (1.0 / 320.0), .y = 0.5 - (69.0 / 240.0)};
  const WindowFraction corner = finisherGaugeCornerOf(point, Aspect);
  CHECK(corner.x == doctest::Approx(0.5 - (22.0 / 320.0)));
  CHECK(corner.y == doctest::Approx(0.5 - (89.0 / 240.0)));
}

TEST_CASE("the charge gauge stays within 100 pixels of the middle") {
  const WindowFraction corner = finisherGaugeCornerOf({.x = 1.0, .y = 0.0}, 16.0 / 9.0);
  CHECK(corner.x == doctest::Approx(0.5 + (100.0 / (240.0 * 16.0 / 9.0))));
  CHECK(corner.y == doctest::Approx(0.5 - (100.0 / 240.0)));
}

TEST_CASE("the charge gauge is published as its corner, empty when it does not show") {
  const auto shown = finisherGaugeSettingsOf(WindowFraction{.x = 0.25, .y = 0.125});
  REQUIRE(shown.size() == 1);
  CHECK(std::get<std::string>(shown.front().value) == "25.00 12.50");
  const auto hidden = finisherGaugeSettingsOf(std::nullopt);
  CHECK(std::get<std::string>(hidden.front().value).empty());
}
