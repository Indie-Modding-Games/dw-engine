#include <dwe/screens/stun_signs.hpp>

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <variant>

using dwe::screens::stunSignSettingsOf;
using dwe::screens::WindowFraction;

TEST_CASE("each stun sign is published as its box over the foot of its post") {
  const std::array feet{WindowFraction{.x = 0.5, .y = 0.5}, WindowFraction{.x = 0.25, .y = 1.0}};
  const auto settings = stunSignSettingsOf(feet, 4.0 / 3.0);
  REQUIRE(settings.size() == 1);
  CHECK(std::get<std::string>(settings.front().value) ==
        "43.75 31.67 12.50 18.33 18.75 81.67 12.50 18.33");
  CHECK(std::get<std::string>(stunSignSettingsOf({}, 16.0 / 9.0).front().value).empty());
}
