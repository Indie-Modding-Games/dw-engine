#pragma once

#include <dwe/core/error.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/bindless.hpp>
#include <dwe/rhi/pipeline.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace dwe::app {

// How a screen primitive mixes with what is behind it: the original's average (texels with STP,
// alpha 128) and additive semi-transparency.
enum class ScenePrimBlend : std::uint8_t { Alpha, Additive };

// A corner of a screen primitive: a point in clip space (the scene camera's projection times its
// view), moved on screen by `offset` original pixels (x right, y down), as the original sizes its
// flashes in screen pixels around a projected point.
struct ScenePrimCorner {
  glm::vec4 clip{0.0F, 0.0F, 0.0F, 1.0F};
  glm::vec2 offset{0.0F};
  // Normalised texture coordinates.
  glm::vec2 uv{0.0F};
};

// One of the original's GPU primitives in a field scene: a triangle, a quad (corners 0 1 2 and
// 2 1 3, as the original's POLY_FT4) or a one-pixel line from corner 0 to corner 1 (LINE_F2).
struct ScenePrim {
  enum class Shape : std::uint8_t { Triangle, Quad, Line };
  Shape shape = Shape::Triangle;
  std::array<ScenePrimCorner, 4> corners{};
  // Multiplies the texels: the original's 0x80 is 1 for a textured primitive; a line's colour.
  glm::vec4 color{1.0F};
  // The renderer's white texture for an untextured one.
  rhi::TextureId texture{};
  ScenePrimBlend blend = ScenePrimBlend::Alpha;
};

// Builds the clip-space corners of a world point, and its view depth (the original's ordering-table
// key: the drawer draws far primitives first).
struct ScenePrimFrame {
  glm::mat4 view{1.0F};
  glm::mat4 projection{1.0F};
  // Target pixels per original screen pixel (render::MapCamera::scale).
  float pixelScale = 1.0F;

  [[nodiscard]] glm::vec4 clip(glm::vec3 world) const {
    return projection * view * glm::vec4{world, 1.0F};
  }
  // How far in front of the camera a world point is (0 behind it).
  [[nodiscard]] float depth(glm::vec3 world) const { return -(view * glm::vec4{world, 1.0F}).z; }
};

// A clip point on the near plane (an original primitive sorted in front of everything) or the far
// one (behind everything), keeping where it shows on screen.
[[nodiscard]] inline glm::vec4 clipInFront(glm::vec4 clip) {
  return {clip.x, clip.y, 0.0F, clip.w};
}
[[nodiscard]] inline glm::vec4 clipBehind(glm::vec4 clip) {
  // Just inside the far plane, so the depth test keeps it.
  constexpr float FarDepth = 0.9999F;
  return {clip.x, clip.y, FarDepth * clip.w, clip.w};
}

// Draws a field scene's primitives in the scene pass, after the models, tested against their depth
// without writing it. The owner replaces them with set() every frame.
class ScenePrimDrawer final : public render::Drawer {
public:
  static core::Result<std::unique_ptr<ScenePrimDrawer>> create(render::Renderer& renderer);

  // The primitives in the order the original sorts them, back to front; the pixel scale sizes their
  // offsets and lines.
  void set(std::vector<ScenePrim> prims, float pixelScale);
  void clear() { prims_.clear(); }
  [[nodiscard]] bool empty() const { return prims_.empty(); }

  void draw(render::DrawContext& context) override;

  // One vertex as the shader reads it.
  struct Vertex {
    glm::vec4 position;
    glm::vec2 uv;
    glm::vec4 color;
    glm::uvec2 texture;
  };

  // The triangles of the primitives for a target of this size (public for the tests): each corner
  // moved by its offset, a line widened to one original pixel; primitives behind the camera
  // dropped.
  [[nodiscard]] static std::vector<Vertex>
  triangles(const std::vector<ScenePrim>& prims, float pixelScale, glm::vec2 extent,
            std::vector<std::pair<ScenePrimBlend, std::uint32_t>>* runs);

private:
  ScenePrimDrawer(rhi::GraphicsPipeline alpha, rhi::GraphicsPipeline additive)
      : alpha_(std::move(alpha)), additive_(std::move(additive)) {}

  rhi::GraphicsPipeline alpha_;
  rhi::GraphicsPipeline additive_;
  std::vector<ScenePrim> prims_;
  float pixelScale_ = 1.0F;
};

} // namespace dwe::app
