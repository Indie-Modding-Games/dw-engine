#pragma once

#include <dwe/core/original_math.hpp>
#include <dwe/game/battle/effect_runner.hpp>

#include <array>
#include <cstdint>

// The fixed-point maths the effect scripts' sub-ops use, as the original does it: angles of 4096
// per turn (512 for math.c's _sin and _cos), matrices of 4096 per unit, the console's axes (y
// down).
namespace dwe::game::battle::effect_math {

using Vector = EffectVector;
using Matrix = EffectMatrix;

// The console's 16-bit vector (SVECTOR): what copyVector keeps of a 32-bit one.
[[nodiscard]] std::int32_t toShort(std::int32_t value);
[[nodiscard]] Vector toShort(const Vector& v);

// math.c: _sin, _cos (512 per turn, 4096 is 1) and _atan (4096 per turn, from 0 to 4095).
[[nodiscard]] std::int32_t sin512(std::int32_t angle);
[[nodiscard]] std::int32_t cos512(std::int32_t angle);
using core::original::atan4096;
// libgte rsin, rcos (4096 per turn) and ratan2 (from -2048 to 2048).
using core::original::rcos;
using core::original::rsin;
[[nodiscard]] std::int32_t ratan2(std::int32_t y, std::int32_t x);

// libgte RotMatrix (Rx·Ry·Rz), RotMatrixYXZ (Ry·Rx·Rz) and RotMatrixZYX (Rz·Ry·Rx), translation 0.
[[nodiscard]] Matrix rotXYZ(const Vector& angles);
[[nodiscard]] Matrix rotYXZ(const Vector& angles);
[[nodiscard]] Matrix rotZYX(const Vector& angles);
// ScaleMatrix: each column times its scale / 4096.
[[nodiscard]] Matrix scaled(Matrix m, const Vector& scale);
// ApplyMatrix (the rotation only, / 4096) and MulMatrix0 (rotations only).
[[nodiscard]] Vector apply(const Matrix& m, const Vector& v);
[[nodiscard]] Matrix multiply(const Matrix& a, const Matrix& b);
// The transpose of the rotation, its inverse.
[[nodiscard]] Matrix transposed(const Matrix& m);

// vecmath.c: matrixToEuler1 (the angles of a RotMatrix) and matrixToEuler2 (of a RotMatrixYXZ).
[[nodiscard]] Vector eulerXYZ(const Matrix& m);
[[nodiscard]] Vector eulerYXZ(const Matrix& m);
// vecmath.c: toEulerAngles: the yaw and pitch that point along a direction.
[[nodiscard]] Vector toEulerAngles(const Vector& direction);
// vecmath.c: getDistance.
[[nodiscard]] std::int32_t distance(const Vector& v);
// vecmath.c: multiplyRotations: the YXZ angles of one turn after another.
[[nodiscard]] Vector combineRotations(const Vector& first, const Vector& second);

// battle_effect.c: BTL_interpolateClamped: from start to end as t goes from lo to hi (either way
// round), held at the ends.
[[nodiscard]] std::int32_t interpolateClamped(std::int32_t lo, std::int32_t hi, std::int32_t t,
                                              std::int32_t start, std::int32_t end);

// effect_common.c: lerp, with its wrap-around.
using core::original::lerp;

} // namespace dwe::game::battle::effect_math
