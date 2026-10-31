#include <doctest/doctest.h>

#include "bird_transport_screen.hpp"

using dwe::app::BirdCursor;

TEST_CASE("bird transport: the cursor stops at the first and the last place") {
  constexpr std::int32_t Rows = 3;
  BirdCursor cursor;
  // itemMenuCursorUp and itemMenuCursorDown: the six rows never scroll nor wrap.
  cursor.up();
  CHECK(cursor.row == 0);
  cursor.down(Rows);
  cursor.down(Rows);
  CHECK(cursor.row == 2);
  cursor.down(Rows);
  CHECK(cursor.row == 2);
  cursor.up();
  CHECK(cursor.row == 1);
  // An empty list keeps the cursor on the first row.
  BirdCursor empty;
  empty.down(0);
  CHECK(empty.row == 0);
}
