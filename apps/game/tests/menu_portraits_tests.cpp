#include <doctest/doctest.h>

#include <string>

#include "menu_portraits.hpp"

namespace dwe::app {

TEST_CASE("the menus say which portrait the game draws over them, and nothing else") {
  constexpr std::int32_t Medals = 15;
  const MenuPortraits on = MenuPortraits::of(true, true, 3.0, Medals);
  CHECK(on.partner);
  CHECK(on.player);
  CHECK(on.medal == 3);
  // A medal the sheet does not have, or no whole number, is none; a toggle without a yes is off.
  const MenuPortraits off = MenuPortraits::of(std::string{"x"}, ui::UiValue{}, 15.0, Medals);
  CHECK_FALSE(off.partner);
  CHECK_FALSE(off.player);
  CHECK_FALSE(off.medal.has_value());
  CHECK_FALSE(MenuPortraits::of(false, false, 1.5, Medals).medal.has_value());
  CHECK_FALSE(MenuPortraits::of(false, false, -1.0, Medals).medal.has_value());
}

} // namespace dwe::app
