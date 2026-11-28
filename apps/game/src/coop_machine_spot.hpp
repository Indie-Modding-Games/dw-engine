#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <span>

namespace dwe::app {

// How far apart two partners on one training machine are drawn (coop-training.md T8).
inline constexpr float MachineSpotStep = 300.0F;

// Each game runs the gym on its own copy of the map, so two players' partners can train on the
// same machine at once. On one screen the second is drawn beside the first: the spot `at`, or the
// nearest free one to either side of the way it faces (`yaw`), one step apart. A spot is taken by
// a partner in `taken` closer than a step.
[[nodiscard]] inline glm::vec3 machineSpotBeside(glm::vec3 at, float yaw,
                                                 std::span<const glm::vec3> taken,
                                                 float step = MachineSpotStep) {
  const glm::vec3 side{std::cos(yaw), 0.0F, -std::sin(yaw)};
  const auto free = [&](glm::vec3 spot) {
    for (const glm::vec3& other : taken) {
      const glm::vec3 apart{spot.x - other.x, 0.0F, spot.z - other.z};
      if (glm::length(apart) < step) {
        return false;
      }
    }
    return true;
  };
  if (free(at)) {
    return at;
  }
  constexpr int MaxSteps = 4;
  for (int steps = 1; steps <= MaxSteps; ++steps) {
    for (const float way : {1.0F, -1.0F}) {
      const glm::vec3 spot = at + (side * (way * step * static_cast<float>(steps)));
      if (free(spot)) {
        return spot;
      }
    }
  }
  return at;
}

} // namespace dwe::app
