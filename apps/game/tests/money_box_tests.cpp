#include <doctest/doctest.h>

#include <string>
#include <variant>

#include "money_box_screen.hpp"

TEST_CASE("script_common.c tickShopBitsBox: the money box says the keeper's line with the money") {
  const dwe::screens::ScreenSettings settings = dwe::app::moneyBoxSettings("maphead.1220.6", 1500);
  REQUIRE(settings.size() == 2);
  CHECK(settings[0].name == "money_box.bits_key");
  CHECK(std::get<std::string>(settings[0].value) == "maphead.1220.6");
  CHECK(settings[1].name == "money_box.money");
  CHECK(std::get<double>(settings[1].value) == doctest::Approx(1500.0));
}
