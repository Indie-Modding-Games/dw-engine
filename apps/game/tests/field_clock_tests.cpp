// The field behind the game menu: the player and the partner rest on their idle clips while the
// map's characters stand still (tamer.c:520, overworld.c:1909 setPartnerIdling; docs/modulos/apps/
// game.md).

#include <doctest/doctest.h>

#include "field_clock.hpp"

using dwe::app::FieldClock;
using dwe::app::fieldClockOf;

TEST_CASE("the game menu rests the player and the partner on their idle clips") {
  CHECK(fieldClockOf({.gameMenu = true}) == FieldClock::MenuRest);
}

TEST_CASE("with nothing open the field runs; any other screen freezes it, under the menu too") {
  CHECK(fieldClockOf({}) == FieldClock::Running);
  CHECK(fieldClockOf({.otherScreen = true}) == FieldClock::Frozen);
  CHECK(fieldClockOf({.gameMenu = true, .otherScreen = true}) == FieldClock::Frozen);
}
