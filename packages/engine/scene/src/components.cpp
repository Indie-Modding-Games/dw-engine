#include <dwe/scene/components.hpp>

#include <cmath>
#include <numbers>

namespace dwe::scene {

namespace {

constexpr float Turn = 2.0F * std::numbers::pi_v<float>;

} // namespace

Pose interpolate(const Transform& transform, float alpha) {
  const float delta = std::remainder(transform.yaw - transform.previousYaw, Turn);
  return {.position = glm::mix(transform.previousPosition, transform.position, alpha),
          .yaw = transform.previousYaw + (delta * alpha)};
}

void beginTick(Transform& transform) {
  transform.previousPosition = transform.position;
  transform.previousYaw = transform.yaw;
}

void place(Transform& transform, glm::vec3 position, float yaw) {
  transform.position = position;
  transform.previousPosition = position;
  transform.yaw = yaw;
  transform.previousYaw = yaw;
}

std::string_view motionName(Motion motion) {
  switch (motion) {
  case Motion::Idle:
    return "idle";
  case Motion::Walk:
    return "walk";
  case Motion::Run:
    return "run";
  }
  return "idle";
}

} // namespace dwe::scene
