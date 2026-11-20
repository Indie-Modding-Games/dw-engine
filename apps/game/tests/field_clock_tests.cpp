// The field behind the game menu: the player and the partner rest on their idle clips while the
// map's characters stand still (tamer.c:520, overworld.c:1909 setPartnerIdling; docs/modules/apps/
// game.md).

#include <doctest/doctest.h>

#include "field_clock.hpp"

using dwe::app::FieldClock;
using dwe::app::fieldClockOf;
using dwe::app::sceneryMoves;

TEST_CASE("the game menu rests the player and the partner on their idle clips") {
  CHECK(fieldClockOf({.gameMenu = true}) == FieldClock::MenuRest);
}

TEST_CASE("with nothing open the field runs; any other screen freezes it, under the menu too") {
  CHECK(fieldClockOf({}) == FieldClock::Running);
  CHECK(fieldClockOf({.otherScreen = true}) == FieldClock::Frozen);
  CHECK(fieldClockOf({.gameMenu = true, .otherScreen = true}) == FieldClock::Frozen);
}

TEST_CASE("a battle stands the characters but the map's sprites, mist and weather move on") {
  // map.c: renderMap draws the map every frame in a battle too, and its renderMapOverlays and
  // renderMist count on (an overlay's timer 189 to 193 in 10 vblanks of GAME_STATE 1, mayo00).
  CHECK(fieldClockOf({.battle = true}) == FieldClock::Battle);
  CHECK(fieldClockOf({.gameMenu = true, .battle = true}) == FieldClock::Battle);
  CHECK(sceneryMoves(FieldClock::Running));
  CHECK(sceneryMoves(FieldClock::Battle));
  // Start's pause or the pause menu over a battle still stop it all (handlePause: an overlay's
  // timer stays at 17 for 32 vblanks).
  CHECK(fieldClockOf({.battle = true, .otherScreen = true}) == FieldClock::Frozen);
  CHECK_FALSE(sceneryMoves(FieldClock::Frozen));
  CHECK_FALSE(sceneryMoves(FieldClock::MenuRest));
}
