#pragma once

#include <dwe/core/original_math.hpp>

#include <cmath>
#include <cstdint>

// The integer maths the KAR overlay leans on: libgte rcos/rsin (4096 a turn, 4096 for 1), ratan2
// and SquareRoot0.
namespace dwe::game::curling::kar {

inline constexpr std::int32_t Turn = 4096;
inline constexpr std::int32_t One = 4096;

[[nodiscard]] inline std::int32_t rcos(std::int32_t angle) {
  return core::original::rcos(angle);
}
[[nodiscard]] inline std::int32_t rsin(std::int32_t angle) {
  return core::original::rsin(angle);
}

// libgte ratan2 (from -2048 to 2048).
[[nodiscard]] inline std::int32_t ratan2(std::int32_t y, std::int32_t x) {
  return core::original::ratan2(y, x);
}

// kar.c: KAR_distance: SquareRoot0 of the sum of the squares (rounded down), in 32 bits.
[[nodiscard]] inline std::int32_t distance(std::int32_t x, std::int32_t y) {
  const auto sum = static_cast<std::uint32_t>(static_cast<std::uint32_t>(x * x) +
                                              static_cast<std::uint32_t>(y * y));
  auto root = static_cast<std::uint64_t>(std::sqrt(static_cast<double>(sum)));
  while (root * root > sum) {
    --root;
  }
  while ((root + 1) * (root + 1) <= sum) {
    ++root;
  }
  return static_cast<std::int32_t>(root);
}

// `length` along `angle`: the original's `length * rcos(angle) / 4096`.
[[nodiscard]] inline std::int32_t alongX(std::int32_t length, std::int32_t angle) {
  return length * rcos(angle) / One;
}
[[nodiscard]] inline std::int32_t alongZ(std::int32_t length, std::int32_t angle) {
  return length * rsin(angle) / One;
}

} // namespace dwe::game::curling::kar
