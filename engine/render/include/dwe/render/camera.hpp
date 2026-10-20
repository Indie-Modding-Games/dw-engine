#pragma once

#include <dwe/core/math.hpp>
#include <dwe/rhi/types.hpp>

namespace dwe::render {

// A view of the world: world → view → Vulkan clip space (Y down, depth 0 to 1), drawn into viewport (pixels).
struct Camera {
    glm::mat4 view{1.0F};
    glm::mat4 projection{1.0F};
    rhi::Viewport viewport{};

    [[nodiscard]] glm::mat4 viewProjection() const { return projection * view; }
};

// A right-handed perspective projection (looking down -Z) into Vulkan's clip space: vertical field of view in
// radians, width / height, and the near and far planes (0 < near < far). +Y in view space is up on screen.
glm::mat4 perspective(float fovY, float aspect, float near, float far);

// Maps pixel coordinates of a target of the given size (origin at the top left, Y down) to clip space. Z is kept.
glm::mat4 pixelProjection(rhi::Extent extent);

// The viewport that covers a whole target.
rhi::Viewport fullViewport(rhi::Extent extent);

} // namespace dwe::render
