#include <doctest/doctest.h>

#include "discard_screen.hpp"

using dwe::app::DiscardListView;
using dwe::app::DiscardVisibleRows;

TEST_CASE(
    "discard list: the cursor walks the six rows and the list scrolls under it at the edges") {
  constexpr std::int32_t Slots = 10;
  DiscardListView list;
  list.up();
  CHECK(list.index() == 0);
  for (std::int32_t i = 0; i < DiscardVisibleRows - 1; ++i) {
    list.down(Slots);
  }
  CHECK(list.top == 0);
  CHECK(list.cursor == DiscardVisibleRows - 1);
  // itemMenuCursorDown: past the window's last row the list moves, the cursor stays.
  list.down(Slots);
  CHECK(list.top == 1);
  CHECK(list.cursor == DiscardVisibleRows - 1);
  for (std::int32_t i = 0; i < Slots; ++i) {
    list.down(Slots);
  }
  CHECK(list.index() == Slots - 1);
  CHECK(list.top == Slots - DiscardVisibleRows);
  // itemMenuCursorUp: the cursor climbs first, then the list.
  list.up();
  CHECK(list.top == Slots - DiscardVisibleRows);
  CHECK(list.cursor == DiscardVisibleRows - 2);
}

TEST_CASE("discard list: R1 held jumps to the first or the last row") {
  DiscardListView list;
  list.toBottom(30);
  CHECK(list.top == 24);
  CHECK(list.cursor == DiscardVisibleRows - 1);
  CHECK(list.index() == 29);
  list.toTop();
  CHECK(list.index() == 0);
  // A bag shorter than the window: the last slot without scrolling.
  list.toBottom(4);
  CHECK(list.top == 0);
  CHECK(list.cursor == 3);
}
