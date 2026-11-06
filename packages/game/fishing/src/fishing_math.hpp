#pragma once

#include <dwe/core/math.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>

// The original's vector helpers on the water's plane (toEulerAngles, rotateVector, getDistance).
namespace dwe::game::fishing {

// The original's fixed point: 0x1000 is one.
inline constexpr std::int32_t FixedOne = 0x1000;
inline constexpr std::int32_t FixedShift = 12;

inline constexpr float RadiansPerOriginalAngle =
    2.0F * std::numbers::pi_v<float> / static_cast<float>(core::OriginalAnglePerTurn);

// The direction a yaw swims along, on the ground.
inline glm::vec3 forward(float yaw) {
  return {std::sin(yaw), 0.0F, std::cos(yaw)};
}

// The yaw from one point towards another, on the ground.
inline float yawTowards(glm::vec3 from, glm::vec3 to) {
  return std::atan2(to.x - from.x, to.z - from.z);
}

inline float groundDistance(glm::vec3 a, glm::vec3 b) {
  return glm::length(glm::vec2{b.x - a.x, b.z - a.z});
}

// An angle brought into [-pi, pi).
inline float wrapAngle(float angle) {
  constexpr float Turn = 2.0F * std::numbers::pi_v<float>;
  angle = std::fmod(angle + std::numbers::pi_v<float>, Turn);
  if (angle < 0.0F) {
    angle += Turn;
  }
  return angle - std::numbers::pi_v<float>;
}

inline std::int32_t toOriginalAngle(float radians) {
  return static_cast<std::int32_t>(std::lround(radians / RadiansPerOriginalAngle));
}

inline float fromOriginalAngle(std::int32_t angle) {
  return static_cast<float>(angle) * RadiansPerOriginalAngle;
}

} // namespace dwe::game::fishing
