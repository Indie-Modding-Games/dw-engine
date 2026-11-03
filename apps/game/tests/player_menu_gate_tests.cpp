#include <doctest/doctest.h>

#include "player_menu_gate.hpp"

using dwe::app::PlayerMenuGate;
using dwe::app::playerMenuShutBy;

namespace {

constexpr PlayerMenuGate Free{.playerWalks = true,
                              .scriptRuns = false,
                              .fadeProtected = false,
                              .dialogueShown = false,
                              .busyWith = {}};

} // namespace

TEST_CASE("player menu gate: it opens only while the player walks freely, as the original") {
  // The walking state's tick (tamer.c:520): Triangle opens the game menu in player state 0 with
  // IS_SCRIPT_PAUSED 1, FADE_PROTECTION 0 and the dialogue box shut.
  CHECK(playerMenuShutBy(Free).empty());

  PlayerMenuGate fading = Free;
  fading.fadeProtected = true;
  CHECK(playerMenuShutBy(fading) == "fade");

  PlayerMenuGate scripted = Free;
  scripted.scriptRuns = true;
  CHECK(playerMenuShutBy(scripted) == "script");

  PlayerMenuGate talking = Free;
  talking.dialogueShown = true;
  CHECK(playerMenuShutBy(talking) == "dialogue");

  PlayerMenuGate battle = Free;
  battle.busyWith = "battle";
  CHECK(playerMenuShutBy(battle) == "battle");

  // A map change's fade in that already gave the player back his walk still keeps it shut until
  // its last frame (fade.c: renderFadeIn clears FADE_PROTECTION one frame later).
  PlayerMenuGate fadeTail = Free;
  fadeTail.fadeProtected = true;
  CHECK_FALSE(playerMenuShutBy(fadeTail).empty());

  // The care scenes, a choice or the first tick of a map hold the player (player state 6 and
  // others).
  CHECK(playerMenuShutBy(PlayerMenuGate{}) == "player_held");
}
