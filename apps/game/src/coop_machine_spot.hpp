#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <span>

namespace dwe::app {

// How far apart two partners on one training machine are drawn (coop-training.md T8).
inline constexpr float MachineSpotStep = 300.0F;

// Two partners can share a machine (each game runs its own gym copy): the second is drawn
// at the nearest spot beside `at` along `yaw`; one in `taken` within a step occupies it.
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

struct MachinePair {
  glm::vec3 player;
  glm::vec3 partner;
};

// Moves the player as far as its partner, so two players at one machine stand apart too.
[[nodiscard]] inline MachinePair machinePairBeside(glm::vec3 player, glm::vec3 partner, float yaw,
                                                   std::span<const glm::vec3> taken,
                                                   float step = MachineSpotStep) {
  const glm::vec3 beside = machineSpotBeside(partner, yaw, taken, step);
  return {.player = player + (beside - partner), .partner = beside};
}

} // namespace dwe::app
