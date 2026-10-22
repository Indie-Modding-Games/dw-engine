#include <dwe/render/camera.hpp>

namespace dwe::render {

glm::mat4 perspective(float fovY, float aspect, float near, float far) {
  // GLM is configured for a 0..1 depth range (engine/core); Vulkan's clip space has Y down, so Y
  // flips.
  glm::mat4 projection = glm::perspectiveRH_ZO(fovY, aspect, near, far);
  projection[1][1] = -projection[1][1];
  return projection;
}

glm::mat4 pixelProjection(rhi::Extent extent) {
  // Vulkan's clip space already has Y down, as pixel coordinates do; Z passes through.
  glm::mat4 projection{1.0F};
  projection[0][0] = 2.0F / static_cast<float>(extent.width);
  projection[1][1] = 2.0F / static_cast<float>(extent.height);
  projection[3][0] = -1.0F;
  projection[3][1] = -1.0F;
  return projection;
}

rhi::Viewport fullViewport(rhi::Extent extent) {
  return {.x = 0.0F,
          .y = 0.0F,
          .width = static_cast<float>(extent.width),
          .height = static_cast<float>(extent.height),
          .minDepth = 0.0F,
          .maxDepth = 1.0F};
}

} // namespace dwe::render
