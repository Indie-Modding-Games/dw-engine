#pragma once

// Vectors, matrices and quaternions for the whole engine: GLM, configured once in
// engine/core/CMakeLists.txt (radians, Vulkan's 0..1 depth range). Include this header instead of
// GLM's own.
//
// World space (docs/architecture/world-space.md): right-handed, +Y up, in the original's units (100
// units = one cell of a map's collision grid). The original's (x, y, z), with Y down, becomes (x,
// -y, -z), as in the extracted glTF models.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace dwe::core {

// One turn in the original's angle units.
inline constexpr int OriginalAnglePerTurn = 4096;

// A point or direction in the original's axes, turned into world space.
constexpr glm::vec3 fromOriginalAxes(glm::vec3 original) {
  return {original.x, -original.y, -original.z};
}

} // namespace dwe::core
