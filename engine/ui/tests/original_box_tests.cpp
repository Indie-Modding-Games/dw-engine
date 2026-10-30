#include <doctest/doctest.h>

#include <algorithm>

#include "original_box.hpp"

using dwe::ui::OriginalBoxLine;
using dwe::ui::originalBoxLines;

namespace {

// The colour of the pixel (x, y) of a box's border, 0 when no line covers it.
unsigned pixelAt(const std::vector<OriginalBoxLine>& lines, int x, int y) {
  unsigned colour = 0;
  for (const OriginalBoxLine& line : lines) {
    const bool covers = line.horizontal ? (y == line.y && x >= line.x && x < line.x + line.length)
                                        : (x == line.x && y >= line.y && y < line.y + line.length);
    if (covers) {
      colour = line.colour;
    }
  }
  return colour;
}

} // namespace

TEST_CASE("the original box's border has renderUIBoxBorder's lines") {
  // The pause box: 56 x 24 (utils2.c: createPauseBox).
  const auto lines = originalBoxLines(56, 24);
  const std::vector<OriginalBoxLine> all{lines.begin(), lines.end()};
  // Top, from x + 4: black, light cyan, black.
  CHECK(pixelAt(all, 4, 0) == 0x020202U);
  CHECK(pixelAt(all, 20, 1) == 0x90D9FAU);
  CHECK(pixelAt(all, 20, 2) == 0x020202U);
  // Bottom, the last three rows.
  CHECK(pixelAt(all, 20, 21) == 0x020202U);
  CHECK(pixelAt(all, 20, 22) == 0x90D9FAU);
  CHECK(pixelAt(all, 52, 23) == 0x020202U);
  // Left: black, dark cyan, light cyan and renderUIBoxStatic's black.
  CHECK(pixelAt(all, 0, 10) == 0x020202U);
  CHECK(pixelAt(all, 1, 10) == 0x4A9FC5U);
  CHECK(pixelAt(all, 2, 10) == 0x90D9FAU);
  CHECK(pixelAt(all, 3, 10) == 0x020202U);
  // Right, mirrored.
  CHECK(pixelAt(all, 52, 10) == 0x020202U);
  CHECK(pixelAt(all, 53, 10) == 0x90D9FAU);
  CHECK(pixelAt(all, 54, 10) == 0x4A9FC5U);
  CHECK(pixelAt(all, 55, 10) == 0x020202U);
  // The corners are the sprites': no line there.
  CHECK(pixelAt(all, 0, 0) == 0U);
  CHECK(pixelAt(all, 55, 23) == 0U);
  // The inside is the fill's.
  CHECK(pixelAt(all, 20, 10) == 0U);
}

TEST_CASE("a box too small for its corners draws no border") {
  CHECK(originalBoxLines(8, 24).empty());
  CHECK(originalBoxLines(56, 7).empty());
}
