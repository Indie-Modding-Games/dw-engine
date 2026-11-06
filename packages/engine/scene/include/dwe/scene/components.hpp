#pragma once

#include <dwe/core/math.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace dwe::scene {

// The components shared by the game systems that move entities and the app that draws them. They
// hold plain values in world space (docs/architecture/world-space.md); the systems that own an
// entity keep their private state in their own components.

// Where an entity stands and the way it faces (yaw in radians about +Y; at 0 it faces +Z). The
// logic runs at a fixed tick; the previous values let the renderer draw between two ticks.
struct Transform {
  glm::vec3 position{};
  float yaw = 0.0F;
  glm::vec3 previousPosition{};
  float previousYaw = 0.0F;
};

// A position and a facing to draw.
struct Pose {
  glm::vec3 position{};
  float yaw = 0.0F;
};

// The pose between the previous tick (alpha 0) and the current one (alpha 1). The yaw turns the
// short way.
Pose interpolate(const Transform& transform, float alpha);

// Starts a tick: the current values become the previous ones.
void beginTick(Transform& transform);

// Places an entity without drawing a move from where it was (a spawn, a teleport).
void place(Transform& transform, glm::vec3 position, float yaw);

// The model drawn for an entity: the asset id of its .glb (`digimon/species_a/model`).
struct ModelRef {
  std::string asset;
};

// What an entity is doing, for its animation.
enum class Motion : std::uint8_t { Idle, Walk, Run };

// The animation name of a motion: idle, walk or run.
std::string_view motionName(Motion motion);

// The animation an entity should play, and how fast relative to the clip's own speed, so feet match
// the ground speed.
struct AnimationIntent {
  Motion motion = Motion::Idle;
  float speedScale = 1.0F;
};

// The entity the player controls.
struct PlayerTag {};

// The partner that follows the player.
struct PartnerTag {};

} // namespace dwe::scene
