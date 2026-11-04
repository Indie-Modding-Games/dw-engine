#include "effect_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <numbers>
#include <utility>

namespace dwe::game::battle::effect_math {

namespace {

constexpr std::int32_t One = 4096;
constexpr double Turn = 4096.0;
constexpr std::int32_t AngleMask = 0xFFF;
constexpr std::int32_t HalfTurn = 0x800;
constexpr std::int32_t QuarterTurn = 0x400;
constexpr std::int32_t ShortBits = 16;
constexpr std::int32_t FixedShift = 12;

std::int32_t fixed(std::int64_t value) {
  return static_cast<std::int32_t>(value >> FixedShift);
}

Matrix product(const Matrix& a, const Matrix& b) {
  Matrix out;
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      std::int64_t sum = 0;
      for (std::size_t k = 0; k < 3; ++k) {
        sum += static_cast<std::int64_t>(a.m.at(i).at(k)) * b.m.at(k).at(j);
      }
      out.m.at(i).at(j) = fixed(sum);
    }
  }
  return out;
}

Matrix aboutX(std::int32_t angle) {
  const std::int32_t s = rsin(angle);
  const std::int32_t c = rcos(angle);
  Matrix m;
  m.m = {{{One, 0, 0}, {0, c, -s}, {0, s, c}}};
  return m;
}

Matrix aboutY(std::int32_t angle) {
  const std::int32_t s = rsin(angle);
  const std::int32_t c = rcos(angle);
  Matrix m;
  m.m = {{{c, 0, s}, {0, One, 0}, {-s, 0, c}}};
  return m;
}

Matrix aboutZ(std::int32_t angle) {
  const std::int32_t s = rsin(angle);
  const std::int32_t c = rcos(angle);
  Matrix m;
  m.m = {{{c, -s, 0}, {s, c, 0}, {0, 0, One}}};
  return m;
}

// The angle whose sine and cosine are the largest of four values over the cosine the angle
// matrixToEuler* divides by (the part both share), as vecmath.c picks it.
std::int32_t sharedCosine(std::int32_t sinA, std::int32_t cosA, std::int32_t sinB,
                          std::int32_t cosB, std::int64_t overSinA, std::int64_t overCosA,
                          std::int64_t overSinB, std::int64_t overCosB) {
  const std::int32_t maximum =
      std::max(std::max(std::abs(sinA), std::abs(cosA)), std::max(std::abs(sinB), std::abs(cosB)));
  if (maximum == 0) {
    return 0;
  }
  if (maximum == std::abs(sinA)) {
    return static_cast<std::int32_t>(overSinA / sinA);
  }
  if (maximum == std::abs(cosA)) {
    return static_cast<std::int32_t>(overCosA / cosA);
  }
  if (maximum == std::abs(sinB)) {
    return static_cast<std::int32_t>(overSinB / sinB);
  }
  return static_cast<std::int32_t>(overCosB / cosB);
}

} // namespace

std::int32_t toShort(std::int32_t value) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << ShortBits) >> ShortBits;
}

Vector toShort(const Vector& v) {
  return {.x = toShort(v.x), .y = toShort(v.y), .z = toShort(v.z)};
}

std::int32_t sin512(std::int32_t angle) {
  return core::original::sin512(angle);
}

std::int32_t cos512(std::int32_t angle) {
  return core::original::cos512(angle);
}

std::int32_t ratan2(std::int32_t y, std::int32_t x) {
  if (y == 0 && x == 0) {
    return 0;
  }
  const double angle = std::atan2(static_cast<double>(y), static_cast<double>(x));
  return static_cast<std::int32_t>(std::lround(angle / (2.0 * std::numbers::pi) * Turn));
}

Matrix rotXYZ(const Vector& angles) {
  return product(product(aboutX(angles.x), aboutY(angles.y)), aboutZ(angles.z));
}

Matrix rotYXZ(const Vector& angles) {
  return product(product(aboutY(angles.y), aboutX(angles.x)), aboutZ(angles.z));
}

Matrix rotZYX(const Vector& angles) {
  return product(product(aboutZ(angles.z), aboutY(angles.y)), aboutX(angles.x));
}

Matrix scaled(Matrix m, const Vector& scale) {
  const std::array<std::int32_t, 3> by{scale.x, scale.y, scale.z};
  for (auto& row : m.m) {
    for (std::size_t j = 0; j < 3; ++j) {
      row.at(j) = fixed(static_cast<std::int64_t>(row.at(j)) * by.at(j));
    }
  }
  return m;
}

Vector apply(const Matrix& m, const Vector& v) {
  const auto row = [&](std::size_t i) {
    return fixed((static_cast<std::int64_t>(m.m.at(i).at(0)) * v.x) +
                 (static_cast<std::int64_t>(m.m.at(i).at(1)) * v.y) +
                 (static_cast<std::int64_t>(m.m.at(i).at(2)) * v.z));
  };
  return {.x = row(0), .y = row(1), .z = row(2)};
}

Matrix multiply(const Matrix& a, const Matrix& b) {
  return product(a, b);
}

Matrix transposed(const Matrix& m) {
  Matrix out;
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      out.m.at(i).at(j) = m.m.at(j).at(i);
    }
  }
  return out;
}

Vector eulerXYZ(const Matrix& m) {
  const auto& a = m.m;
  if (a[2][2] == 0 && a[0][0] == 0) {
    return {.x = ratan2(a[2][1], a[1][1]), .y = a[0][2] > 0 ? QuarterTurn : -QuarterTurn, .z = 0};
  }
  Vector out;
  out.x = ratan2(-a[1][2], a[2][2]);
  out.z = ratan2(-a[0][1], a[0][0]);
  const std::int64_t shifted = std::int64_t{One};
  const std::int32_t value =
      sharedCosine(rsin(out.x), rcos(out.x), rsin(out.z), rcos(out.z), a[1][2] * -shifted,
                   a[2][2] * shifted, a[0][1] * -shifted, a[0][0] * shifted);
  out.y = ratan2(a[0][2], value);
  return out;
}

Vector eulerYXZ(const Matrix& m) {
  const auto& a = m.m;
  if (a[2][2] == 0 && a[0][2] == 0) {
    return {.x = a[1][2] > 0 ? -QuarterTurn : QuarterTurn, .y = ratan2(-a[2][0], a[0][0]), .z = 0};
  }
  Vector out;
  out.y = ratan2(a[0][2], a[2][2]);
  out.z = ratan2(a[1][0], a[1][1]);
  const std::int64_t shifted = std::int64_t{One};
  const std::int32_t value =
      sharedCosine(rsin(out.y), rcos(out.y), rsin(out.z), rcos(out.z), a[0][2] * shifted,
                   a[2][2] * shifted, a[1][0] * shifted, a[1][1] * shifted);
  out.x = ratan2(-a[1][2], value);
  return out;
}

Vector toEulerAngles(const Vector& direction) {
  Vector out;
  out.y = ratan2(direction.x, direction.z);
  const std::int32_t ground = distance({.x = direction.x, .y = 0, .z = direction.z});
  out.x = -ratan2(direction.y, ground);
  out.z = 0;
  const auto centred = [](std::int32_t angle) {
    angle &= AngleMask;
    return angle >= HalfTurn ? angle - One : angle;
  };
  out.x = centred(out.x);
  out.y = centred(out.y);
  return out;
}

// The original's square root (a table and two Newton steps) gives the integer root within a unit;
// this is the exact one.
std::int32_t distance(const Vector& v) {
  const double squared = (static_cast<double>(v.x) * v.x) + (static_cast<double>(v.y) * v.y) +
                         (static_cast<double>(v.z) * v.z);
  return static_cast<std::int32_t>(std::sqrt(squared));
}

Vector combineRotations(const Vector& first, const Vector& second) {
  if (second.x == 0 && second.y == 0 && second.z == 0) {
    return first;
  }
  return eulerYXZ(product(rotYXZ(first), rotYXZ(second)));
}

std::int32_t interpolateClamped(std::int32_t lo, std::int32_t hi, std::int32_t t,
                                std::int32_t start, std::int32_t end) {
  if (hi < lo) {
    std::swap(lo, hi);
  }
  t = std::clamp(t, lo, hi);
  if (lo >= t) {
    return start;
  }
  return start + ((end - start) * (t - lo) / (hi - lo));
}

} // namespace dwe::game::battle::effect_math
