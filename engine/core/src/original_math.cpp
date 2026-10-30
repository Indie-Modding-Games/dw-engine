#include <dwe/core/original_math.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <numbers>

namespace dwe::core::original {

namespace {

constexpr std::int32_t One = 4096;
constexpr double Turn = 4096.0;
constexpr std::int32_t AngleMask = 0xFFF;
constexpr std::int32_t HalfTurn = 0x800;
constexpr std::int32_t QuarterTurn = 0x400;

// math.c: ATAN_LOOKUP_TABLE: entry i is atan(i / 512) in angle units (4096 a turn), rounded
// (checked against the disc's table: all 512 entries).
constexpr std::int32_t AtanSteps = 512;

std::int32_t atanTable(std::int32_t index) {
  const double radians = std::atan(static_cast<double>(index) / AtanSteps);
  return static_cast<std::int32_t>(std::lround(radians / (2.0 * std::numbers::pi) * Turn));
}

} // namespace

std::int32_t atan4096(std::int32_t dy, std::int32_t dx) {
  // The smaller side over the larger one, in 512ths and rounded down, picks an eighth of a turn's
  // angle from the table; the quadrants mirror it.
  const std::int64_t absDy = std::abs(static_cast<std::int64_t>(dy));
  const std::int64_t absDx = std::abs(static_cast<std::int64_t>(dx));
  const auto eighth = [](std::int64_t small, std::int64_t large) {
    const std::int64_t index = large > 0 ? (small * AtanSteps) / large : 0;
    return atanTable(static_cast<std::int32_t>(std::min<std::int64_t>(index, AtanSteps - 1)));
  };
  std::int32_t angle =
      absDy >= absDx ? eighth(absDx, absDy) : QuarterTurn - 1 - eighth(absDy, absDx);
  if (dy < 0) {
    angle = HalfTurn - angle;
  }
  if (dx < 0) {
    angle = -angle;
  }
  return (angle + HalfTurn) & AngleMask;
}

std::int32_t rsin(std::int32_t angle) {
  // libgte's table holds sin(x) * 4096 rounded; computing it gives the same values within a unit
  // (suposición: the table's own rounding is not checked entry by entry).
  return static_cast<std::int32_t>(std::lround(
      std::sin(static_cast<double>(angle & AngleMask) / Turn * 2.0 * std::numbers::pi) * One));
}

std::int32_t rcos(std::int32_t angle) {
  return rsin(angle + QuarterTurn);
}

} // namespace dwe::core::original
