// The original's table trigonometry (math.c): _sin and _cos read a quarter turn of sines rounded
// down, measured in the original (SIN_LOOKUP_TABLE, all 129 entries; docs/modulos/engine/core.md).

#include <dwe/core/original_math.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <numbers>

namespace {

using dwe::core::original::cos512;
using dwe::core::original::sin512;

} // namespace

TEST_CASE("math.c _sin reads the original's table: sines rounded down, not to the nearest") {
  // Entries read from the original's SIN_LOOKUP_TABLE.
  CHECK(sin512(0) == 0);
  CHECK(sin512(1) == 50);
  CHECK(sin512(2) == 100); // 100.53 rounded down; to the nearest it would be 101
  CHECK(sin512(5) == 251);
  CHECK(sin512(64) == 2896);
  CHECK(sin512(67) == 3000);  // 3000.9
  CHECK(sin512(127) == 4095); // 4095.7
  CHECK(sin512(128) == 4096);
  // Every entry is the sine rounded down.
  for (std::int32_t i = 0; i <= 128; ++i) {
    const double sine = std::sin(static_cast<double>(i) / 512.0 * 2.0 * std::numbers::pi) * 4096.0;
    CHECK(sin512(i) == static_cast<std::int32_t>(std::floor(sine + 1e-9)));
  }
}

TEST_CASE("math.c _sin mirrors its quarter: the negative half is the table's opposite") {
  CHECK(sin512(256 - 2) == 100);
  CHECK(sin512(256 + 2) == -100);
  CHECK(sin512(512 - 2) == -100);
  CHECK(sin512(256) == 0);
  CHECK(sin512(384) == -4096);
  // The angle wraps to a turn of 512, negative angles too.
  CHECK(sin512(512 + 2) == 100);
  CHECK(sin512(-2) == -100);
}

TEST_CASE("math.c _cos is _sin a quarter turn on") {
  CHECK(cos512(0) == 4096);
  CHECK(cos512(128) == 0);
  CHECK(cos512(126) == 100);
  CHECK(cos512(256) == -4096);
  CHECK(cos512(-126) == 100);
}
