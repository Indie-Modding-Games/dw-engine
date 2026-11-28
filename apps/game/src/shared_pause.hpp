#pragma once

namespace dwe::app {

struct LocalScreens {
  bool gameMenu = false;
  bool mainMenu = false;
  bool partnerMenu = false;
  bool pauseBox = false;
};

// Alone they stop the game, as in the original (utils2.c: handlePause, tamer.c:520). In a shared
// world (multiplayer.md decision 5) only that player's input waits.
[[nodiscard]] constexpr bool localScreensHoldWorld(const LocalScreens& open, bool worldShared) {
  return !worldShared && (open.gameMenu || open.mainMenu || open.partnerMenu || open.pauseBox);
}

} // namespace dwe::app
