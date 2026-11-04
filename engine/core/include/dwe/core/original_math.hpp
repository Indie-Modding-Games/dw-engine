#pragma once

#include <cstdint>

// The original's integer trigonometry, which gameplay that must land on the original's numbers
// (facings, steps along a facing) uses instead of floating point: angles of 4096 a turn, sines and
// cosines of 4096 for 1.
namespace dwe::core::original {

// math.c:32 (_atan): the angle from +dy toward +dx, half a turn on, from 0 to 4095, read from its
// table of 512 steps per eighth of a turn.
[[nodiscard]] std::int32_t atan4096(std::int32_t dy, std::int32_t dx);
// libgte ratan2: the angle of (x, y) from -2048 to 2048 (4096 a turn), read from a table of 1024
// steps per eighth of a turn at the smaller side over the larger one, rounded down.
[[nodiscard]] std::int32_t ratan2(std::int32_t y, std::int32_t x);
// math.c:13 (_sin) and math.c:27 (_cos, a quarter turn on): 512 a turn, 4096 for 1, read from a
// table of 129 steps over a quarter turn whose entries are sin rounded down (measured in the
// original: all 129 entries of SIN_LOOKUP_TABLE); the other quarters mirror it, so a negative sine
// is the opposite of the table's, not rounded down again.
[[nodiscard]] std::int32_t sin512(std::int32_t angle);
[[nodiscard]] std::int32_t cos512(std::int32_t angle);
// libgte rsin and rcos (4096 a turn, 4096 for 1).
[[nodiscard]] std::int32_t rsin(std::int32_t angle);
[[nodiscard]] std::int32_t rcos(std::int32_t angle);

// effect_common.c:278 (lerp): from `start` at t0 to `end` at t1, 0 when t0 and t1 meet. Kept whole:
// the product of the span and the time keeps only its low 32 bits, as the processor's multiply
// does, and past t1 the result wraps around the span instead of stopping at `end`.
[[nodiscard]] std::int32_t lerp(std::int32_t start, std::int32_t end, std::int32_t t0,
                                std::int32_t t1, std::int32_t t);

} // namespace dwe::core::original
