#include <dwe/render/scene_grade.hpp>
#include <dwe/shaders/scene_grade.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace dwe::render {

namespace {

// One light as scene_grade.slang reads it.
struct GpuLight {
  glm::vec2 centre;
  float radius;
  float strength;
  glm::vec4 color;
};
static_assert(sizeof(GpuLight) == 32);

struct GradePush {
  std::uint64_t lights;
  std::uint64_t padding;
  glm::vec4 tint;
  glm::vec4 grade;
  glm::ivec2 lastPixel;
  std::int32_t bloomReach;
  std::uint32_t scene;
  std::uint32_t lightCount;
};

// The bloom's reach in pixels at 240 lines: it grows with the window, as the scene does.
constexpr float BloomReachPerLine = 3.0F / 240.0F;

} // namespace

std::vector<WindowLight> windowLights(std::span<const BackgroundLight> lights,
                                      const MapCamera& camera) {
  std::vector<WindowLight> shown;
  const glm::vec2 viewport = camera.viewportSize();
  for (const BackgroundLight& light : lights) {
    const float radius = light.radius * camera.scale();
    const glm::vec2 centre = camera.backgroundToWindow(light.pixel);
    const bool outside = centre.x + radius < 0.0F || centre.y + radius < 0.0F ||
                         centre.x - radius > viewport.x || centre.y - radius > viewport.y;
    if (outside || radius <= 0.0F || light.strength <= 0.0F) {
      continue;
    }
    shown.push_back(
        {.centre = centre, .radius = radius, .color = light.color, .strength = light.strength});
  }
  if (shown.size() > SceneGrade::MaxLights) {
    std::ranges::partial_sort(shown, shown.begin() + SceneGrade::MaxLights, std::greater{},
                              &WindowLight::strength);
    shown.resize(SceneGrade::MaxLights);
  }
  return shown;
}

core::Result<std::unique_ptr<SceneGrade>> SceneGrade::create(Renderer& renderer) {
  auto pipeline = renderer.createPipeline(
      {
          .spirv = shaders::SceneGrade,
          .vertex = {},
          .blend = rhi::BlendMode::Opaque,
          .name = "scene grade",
      },
      Pass::Post);
  if (!pipeline) {
    return std::unexpected{pipeline.error()};
  }
  return std::unique_ptr<SceneGrade>{new SceneGrade{std::move(*pipeline)}};
}

void SceneGrade::setLights(std::span<const BackgroundLight> lights, const MapCamera* camera) {
  lights_.assign(lights.begin(), lights.end());
  camera_ = camera;
}

void SceneGrade::draw(DrawContext& context) {
  const rhi::TextureId scene = context.sceneColor();
  if (context.pass() != Pass::Post || !scene.valid()) {
    return;
  }
  std::vector<GpuLight> gpuLights;
  if (camera_ != nullptr) {
    for (const WindowLight& light : windowLights(lights_, *camera_)) {
      gpuLights.push_back({.centre = light.centre,
                           .radius = light.radius,
                           .strength = light.strength,
                           .color = glm::vec4{light.color, 1.0F}});
    }
  }
  const auto count = static_cast<std::uint32_t>(gpuLights.size());
  // The shader reads no light when there is none, but its pointer must still name memory.
  if (gpuLights.empty()) {
    gpuLights.push_back({});
  }
  const auto lights = context.upload().upload(std::span<const GpuLight>{gpuLights});
  if (!lights) {
    return;
  }
  const auto width = static_cast<std::int32_t>(context.extent().width);
  const auto height = static_cast<std::int32_t>(context.extent().height);
  rhi::CommandList& commands = context.commands();
  commands.bindPipeline(pipeline_);
  commands.pushConstants(GradePush{
      .lights = lights->address,
      .padding = 0,
      .tint = glm::vec4{params_.tint, 1.0F},
      .grade = {params_.vibrance, params_.contrast, params_.bloom, params_.bloomThreshold},
      .lastPixel = {std::max(width - 1, 0), std::max(height - 1, 0)},
      .bloomReach = std::max(1, static_cast<std::int32_t>(std::lround(height * BloomReachPerLine))),
      .scene = scene.index,
      .lightCount = count,
  });
  commands.draw(3);
}

} // namespace dwe::render
