#pragma once

namespace dwe::app {

// What one player's own screens are open: the game menu (Tab), the main and partner menus, and
// Start's pause box.
struct LocalScreens {
  bool gameMenu = false;
  bool mainMenu = false;
  bool partnerMenu = false;
  bool pauseBox = false;
};

// Whether those screens stop the game. Alone they do, as in the original (utils2.c: handlePause,
// tamer.c:520). In a world shared with other players (co-op, multiplayer.md decision 5) they are
// that player's only: the world, its clock and its battles go on for everyone, and only that
// player's input waits (#415).
[[nodiscard]] constexpr bool localScreensHoldWorld(const LocalScreens& open, bool worldShared) {
  return !worldShared && (open.gameMenu || open.mainMenu || open.partnerMenu || open.pauseBox);
}

} // namespace dwe::app
