#pragma once

#include <cstdint>

namespace dwe::app {

// How the field's characters animate on a frame. Behind the game menu the original goes on drawing
// the player and the partner on their idle clips and leaves the rest still: tamer.c:520 (the
// walking state's tick) opens the menu with the player's startAnimation(0) and overworld.c:1909
// (setPartnerIdling, the partner's startAnimation(0)); partner.c: partnerTickOverworld then only
// plays the partner's clip on while IS_IN_MENU, tamer.c: tamerTickIdle holds the player's, and
// overworld.c: NPCEntityTickOverworld returns at once, so the map's characters stand frozen.
// Measured in the original (twna01, the menu opened while both ran: anim 3 and 4 turn to 0 and
// their frames go on, 2 to 12 and 2 to 6 over 60 vblanks, while the NPCs' stay put).
enum class FieldClock : std::uint8_t {
  Running,  // the field ticks
  MenuRest, // the game menu: the player and the partner on their idle clips, the rest still
  Battle,   // a battle: the field's characters stand, the scenery's own motion runs on
  Frozen,   // any other screen or the title: nothing moves
};

// What holds the field this frame.
struct FieldPauseFacts {
  // The game menu (Tab), or the partner's status page opened from it (still IS_IN_MENU).
  bool gameMenu = false;
  // A battle on the field (its intro banner included).
  bool battle = false;
  // Anything else that stops the field: a shop or a scene's screen, Start's pause (utils2.c:
  // handlePause spins until the next press, nothing drawn moves), the pause or the title menu.
  bool otherScreen = false;
};

[[nodiscard]] constexpr FieldClock fieldClockOf(const FieldPauseFacts& facts) {
  if (facts.otherScreen) {
    return FieldClock::Frozen;
  }
  if (facts.battle) {
    return FieldClock::Battle;
  }
  return facts.gameMenu ? FieldClock::MenuRest : FieldClock::Running;
}

// Whether the scenery's own motion goes on: the map's animated sprites, the mist, and the weather
// and the season's falling leaves drawn over them. The original draws the map with map.c:
// renderMap every frame whatever GAME_STATE says, and its renderMapOverlays steps the sprites'
// timers and renderMist drifts the mist with PLAYTIME_FRAMES, both counting on in a battle
// (measured in mayo00: an overlay's timer 189 to 193 in 10 vblanks of GAME_STATE 1, 164 to 168 as
// the battle starts, PLAYTIME_FRAMES 5070 to 5073).
[[nodiscard]] constexpr bool sceneryMoves(FieldClock clock) {
  return clock == FieldClock::Running || clock == FieldClock::Battle;
}

} // namespace dwe::app
