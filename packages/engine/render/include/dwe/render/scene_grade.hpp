#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/pipeline.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace dwe::render {

// What the light and colour pass does to the scene. The defaults leave it as drawn.
struct SceneGradeParams {
  // Multiplies every pixel (DaylightGrade::tint).
  glm::vec3 tint{1.0F};
  // How much more saturated muted colours get (0 none); vivid ones gain less, greys nothing.
  float vibrance = 0.0F;
  // How far toward an S curve (0 none, 1 smoothstep), which keeps black and white.
  float contrast = 0.0F;
  // How much of the brightest areas' glow (above bloomThreshold) spills around them (0 none).
  float bloom = 0.0F;
  float bloomThreshold = 0.72F;
};

// A point light over the scene, in background pixels (MapLight placed and lit for this frame).
struct BackgroundLight {
  glm::vec2 pixel{0.0F};
  float radius = 0.0F;
  glm::vec3 color{1.0F};
  float strength = 0.0F;
};

// A point light in window pixels, as the pass draws it.
struct WindowLight {
  glm::vec2 centre{0.0F};
  float radius = 0.0F;
  glm::vec3 color{1.0F};
  float strength = 0.0F;
};

// The lights as a camera shows them: placed with its scroll, sized with its scale, and left out
// when they fall wholly outside the window or give no light.
std::vector<WindowLight> windowLights(std::span<const BackgroundLight> lights,
                                      const MapCamera& camera);

// The field's light and colour pass (docs/architecture/render.md, «Paso de luz y color»; an engine
// improvement): FrameDesc::post, drawing the scene's texture into the window with the time of
// day's tint, point lights, vibrance, contrast and bloom (shaders/scene_grade.slang). The UI comes
// after it and is never touched.
class SceneGrade final : public Drawer {
public:
  // At most this many lights a frame; the strongest are kept.
  static constexpr std::size_t MaxLights = 16;

  static core::Result<std::unique_ptr<SceneGrade>> create(Renderer& renderer);

  void setParams(const SceneGradeParams& params) { params_ = params; }
  [[nodiscard]] const SceneGradeParams& params() const { return params_; }

  // The lights of the next frames, placed with `camera` when the pass draws (after the scene
  // sized it to the frame). The camera must outlive its use; nothing draws no lights.
  void setLights(std::span<const BackgroundLight> lights, const MapCamera* camera);

  void draw(DrawContext& context) override;

private:
  explicit SceneGrade(rhi::GraphicsPipeline pipeline) : pipeline_(std::move(pipeline)) {}

  rhi::GraphicsPipeline pipeline_;
  SceneGradeParams params_;
  std::vector<BackgroundLight> lights_;
  const MapCamera* camera_ = nullptr;
};

} // namespace dwe::render
