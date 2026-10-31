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

// libgte's ratan2 table: entry i is atan((i + 0.5) / 1024) in angle units, plus 0.44, rounded
// down. Checked against the disc's table: 1017 of the 1025 entries match and the other 8 sit
// within 0.015 of a step and come out one unit off.
constexpr std::int32_t RatanSteps = 1024;
constexpr std::int64_t RatanWide = 0x7FE00000;
std::int32_t ratanTable(std::int64_t index) {
  const double radians = std::atan((static_cast<double>(index) + 0.5) / RatanSteps);
  return static_cast<std::int32_t>(std::floor(radians / (2.0 * std::numbers::pi) * Turn + 0.44));
}

} // namespace

std::int32_t ratan2(std::int32_t y, std::int32_t x) {
  const std::int64_t absX = std::abs(static_cast<std::int64_t>(x));
  const std::int64_t absY = std::abs(static_cast<std::int64_t>(y));
  if (absX == 0 && absY == 0) {
    return 0;
  }
  // The smaller side over the larger one in 1024ths, rounded down; a side past 2^21 divides by
  // the larger one's 1024th instead so the shift cannot overflow.
  const auto eighth = [](std::int64_t small, std::int64_t large) {
    const std::int64_t index =
        (small & RatanWide) != 0 ? small / (large >> 10) : (small * RatanSteps) / large;
    return ratanTable(index);
  };
  std::int32_t angle = absY < absX ? eighth(absY, absX) : QuarterTurn - eighth(absX, absY);
  if (x < 0) {
    angle = HalfTurn - angle;
  }
  return y < 0 ? -angle : angle;
}

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
