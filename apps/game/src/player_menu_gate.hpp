#pragma once

#include <string_view>

namespace dwe::app {

// What the game menu's gate looks at on the field (the walking state's tick, tamer.c:520, opens it
// on Triangle only when all of them let it; Tab on PC).
struct PlayerMenuGate {
  // The field let the player walk freely in its last tick (player state 0): no script, care scene,
  // map change, choice or fade in holding him.
  bool playerWalks = false;
  // A script runs (IS_SCRIPT_PAUSED is not 1): a talk, an event, a cell's section.
  bool scriptRuns = false;
  // A map change's fade runs (fade.c: FADE_PROTECTION), or any other fade darkens the screen.
  bool fadeProtected = false;
  // The dialogue box is open or still animating (UI_BOX_DATA[0]: state 1 or a frame running).
  bool dialogueShown = false;
  // Anything else that runs instead of the field (a battle, a scene, a screen), as
  // PlaySession::busyWith names it, or empty.
  std::string_view busyWith;
};

// Why the game menu stays shut now, or empty when the menu action opens it. The menu button (Tab,
// the pad's Triangle) and the HUD's menu button go through it alike.
[[nodiscard]] constexpr std::string_view playerMenuShutBy(const PlayerMenuGate& gate) {
  if (gate.fadeProtected) {
    return "fade";
  }
  if (gate.scriptRuns) {
    return "script";
  }
  if (gate.dialogueShown) {
    return "dialogue";
  }
  if (!gate.busyWith.empty()) {
    return gate.busyWith;
  }
  if (!gate.playerWalks) {
    return "player_held";
  }
  return {};
}

} // namespace dwe::app
