#include <doctest/doctest.h>

#include "field_clock.hpp"
#include "shared_pause.hpp"

using dwe::app::FieldClock;
using dwe::app::fieldClockOf;
using dwe::app::LocalScreens;
using dwe::app::localScreensHoldWorld;

TEST_CASE("alone, the menus and Start's pause stop the game as in the original") {
  CHECK(localScreensHoldWorld({.gameMenu = true}, false));
  CHECK(localScreensHoldWorld({.mainMenu = true}, false));
  CHECK(localScreensHoldWorld({.partnerMenu = true}, false));
  CHECK(localScreensHoldWorld({.pauseBox = true}, false));
  CHECK_FALSE(localScreensHoldWorld({}, false));
}

TEST_CASE("in a shared world one player's menus and pause stop nothing of it (#415)") {
  const LocalScreens all{.gameMenu = true, .mainMenu = true, .partnerMenu = true, .pauseBox = true};
  CHECK_FALSE(localScreensHoldWorld(all, true));
  // The field behind them runs, so the world's clock and a battle another player starts go on.
  CHECK(fieldClockOf({.gameMenu = localScreensHoldWorld({.gameMenu = true}, true),
                      .otherScreen = localScreensHoldWorld({.pauseBox = true}, true)}) ==
        FieldClock::Running);
  CHECK(fieldClockOf({.gameMenu = localScreensHoldWorld({.gameMenu = true}, false)}) ==
        FieldClock::MenuRest);
}
