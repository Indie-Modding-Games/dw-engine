#pragma once

#include <dwe/core/math.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/buffer.hpp>
#include <dwe/rhi/pipeline.hpp>
#include <dwe/rhi/types.hpp>
#include <dwe/ui/ui_renderer.hpp>

#include <RmlUi/Core/Vertex.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dwe::ui {

// Geometry RmlUi compiled, in GPU buffers made once when it is compiled. Dropping it goes through
// the device's deferred destruction, so frames in flight can still draw it.
struct UiGeometry {
  rhi::Buffer vertices;
  rhi::Buffer indices;
  std::uint32_t indexCount = 0;
};

// One RenderGeometry call, recorded by Ui::update() and replayed by UiRenderer::draw().
struct UiDraw {
  std::shared_ptr<const UiGeometry> geometry;
  glm::vec2 translation{0.0F};
  render::TextureId texture;
  // Whether the texture is the disc's pixel art, drawn with sharp texel edges at any size
  // (rml.slang: sharpUv).
  bool sharp = false;
  // In pixels from the top left; nothing for the whole target.
  std::optional<rhi::Rect> scissor;
  // RmlUi's transform of the element, in pixels.
  glm::mat4 transform{1.0F};
};

struct UiRenderer::Impl {
  render::Renderer* renderer = nullptr;
  rhi::GraphicsPipeline pipeline;
  std::vector<UiDraw> draws;
  std::map<std::string, render::TextureId, std::less<>> borrowedTextures;
};

} // namespace dwe::ui
