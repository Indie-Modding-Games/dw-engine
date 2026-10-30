#include <doctest/doctest.h>

#include "trade_screen.hpp"

using dwe::app::TradeCursor;

TEST_CASE("trade menu: the cursor stops at the first and the last of the three rows") {
  constexpr std::int32_t Rows = 3;
  TradeCursor cursor;
  // itemMenuCursorUp and itemMenuCursorDown: the trade list does not wrap.
  cursor.up();
  CHECK(cursor.row == 0);
  cursor.down(Rows);
  cursor.down(Rows);
  CHECK(cursor.row == 2);
  cursor.down(Rows);
  CHECK(cursor.row == 2);
  cursor.up();
  CHECK(cursor.row == 1);
  // An empty table keeps the cursor on the first row.
  TradeCursor empty;
  empty.down(0);
  CHECK(empty.row == 0);
}
