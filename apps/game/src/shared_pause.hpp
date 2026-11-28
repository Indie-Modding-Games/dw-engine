#pragma once

namespace dwe::app {

struct LocalScreens {
  bool gameMenu = false;
  bool mainMenu = false;
  bool partnerMenu = false;
  bool pauseBox = false;
  bool dialog = false;
};

// Alone they stop the game, as in the original (utils2.c: handlePause, tamer.c:520). In a shared
// world (multiplayer.md decision 5) only that player's input waits.
[[nodiscard]] constexpr bool localScreensHoldWorld(const LocalScreens& open, bool worldShared) {
  return !worldShared && (open.gameMenu || open.mainMenu || open.partnerMenu || open.pauseBox);
}

// A menu or a partner page covers the field: the other players' tags hide under it.
[[nodiscard]] constexpr bool localMenusOpen(const LocalScreens& open) {
  return open.gameMenu || open.mainMenu || open.partnerMenu;
}

// A player with a menu, a partner page or a dialog open lends its partner to no co-op battle.
[[nodiscard]] constexpr bool localScreensBusy(const LocalScreens& open) {
  return localMenusOpen(open) || open.dialog;
}

} // namespace dwe::app
