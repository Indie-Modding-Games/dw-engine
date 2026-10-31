#pragma once

#include <cstdint>

// The original's integer trigonometry, which gameplay that must land on the original's numbers
// (facings, steps along a facing) uses instead of floating point: angles of 4096 a turn, sines and
// cosines of 4096 for 1.
namespace dwe::core::original {

// math.c: _atan: the angle from +dy toward +dx, half a turn on, from 0 to 4095, read from its
// table of 512 steps per eighth of a turn.
[[nodiscard]] std::int32_t atan4096(std::int32_t dy, std::int32_t dx);
// libgte ratan2: the angle of (x, y) from -2048 to 2048 (4096 a turn), read from a table of 1024
// steps per eighth of a turn at the smaller side over the larger one, rounded down.
[[nodiscard]] std::int32_t ratan2(std::int32_t y, std::int32_t x);
// libgte rsin and rcos (4096 a turn, 4096 for 1).
[[nodiscard]] std::int32_t rsin(std::int32_t angle);
[[nodiscard]] std::int32_t rcos(std::int32_t angle);

} // namespace dwe::core::original
