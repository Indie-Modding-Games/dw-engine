#pragma once

#include <dwe/core/error.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/bindless.hpp>
#include <dwe/rhi/pipeline.hpp>

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <vector>

#include "weather_particles.hpp"

namespace dwe::app {

// Draws the field's weather (weatherQuads) in the scene pass after the map, the characters and
// their effects' depth-tested parts, before the screen primitives (mist, flashes, the story's
// fades) and the UI: rain and snow fall over everything on the map and under the HUD. No depth
// test: the weather is a screen layer. An engine improvement; the original has no weather.
class WeatherDrawer final : public render::Drawer {
public:
  // The renderer must outlive the drawer.
  static core::Result<std::unique_ptr<WeatherDrawer>> create(render::Renderer& renderer);
  ~WeatherDrawer() override;
  WeatherDrawer(const WeatherDrawer&) = delete;
  WeatherDrawer& operator=(const WeatherDrawer&) = delete;
  WeatherDrawer(WeatherDrawer&&) = delete;
  WeatherDrawer& operator=(WeatherDrawer&&) = delete;

  // The quads of this frame, replacing the last ones, cut to the part of the window the map covers
  // (window pixels), so nothing falls on the bars of a wider window.
  void set(std::vector<WeatherQuad> quads, glm::vec2 clipOrigin, glm::vec2 clipSize) {
    quads_ = std::move(quads);
    clipOrigin_ = clipOrigin;
    clipSize_ = clipSize;
  }
  void clear() { quads_.clear(); }
  [[nodiscard]] bool empty() const { return quads_.empty(); }
  // The cloud picture's texture, made on first use (cloudNoisePixels).
  [[nodiscard]] std::optional<rhi::TextureId> clouds() const { return clouds_; }

  void draw(render::DrawContext& context) override;

  struct Vertex {
    glm::vec4 position;
    glm::vec2 uv;
    glm::vec4 color;
    glm::uvec2 info;
    glm::vec4 shadow;
  };

  // The triangles of the quads for a target of this size, and the runs of one blend mode (public
  // for the tests).
  [[nodiscard]] static std::vector<Vertex>
  triangles(const std::vector<WeatherQuad>& quads, glm::vec2 extent, std::uint32_t clouds,
            std::vector<std::pair<WeatherBlendMode, std::uint32_t>>* runs);

private:
  WeatherDrawer(render::Renderer& renderer, rhi::GraphicsPipeline alpha,
                rhi::GraphicsPipeline additive, std::optional<rhi::TextureId> clouds)
      : renderer_(&renderer), alpha_(std::move(alpha)), additive_(std::move(additive)),
        clouds_(clouds) {}

  render::Renderer* renderer_;
  rhi::GraphicsPipeline alpha_;
  rhi::GraphicsPipeline additive_;
  std::optional<rhi::TextureId> clouds_;
  std::vector<WeatherQuad> quads_;
  glm::vec2 clipOrigin_{0.0F};
  glm::vec2 clipSize_{0.0F};
};

} // namespace dwe::app
