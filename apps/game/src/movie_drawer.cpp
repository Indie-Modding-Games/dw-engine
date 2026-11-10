#include "movie_drawer.hpp"

#include <dwe/render/camera.hpp>
#include <dwe/shaders/movie_sharpen.hpp>
#include <dwe/shaders/movie_upscale.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace dwe::app {

namespace {

// render::QuadVertex as the upscaler reads it: without the colour, which movie_upscale.slang does
// not read (the validation layers warn about an attribute a shader does not consume).
constexpr std::array QuadAttributes{
    rhi::VertexAttribute{.location = 0,
                         .format = rhi::VertexFormat::Float2,
                         .offset =
                             static_cast<std::uint32_t>(offsetof(render::QuadVertex, position))},
    rhi::VertexAttribute{.location = 1,
                         .format = rhi::VertexFormat::Float2,
                         .offset = static_cast<std::uint32_t>(offsetof(render::QuadVertex, uv))},
    rhi::VertexAttribute{.location = 3,
                         .format = rhi::VertexFormat::Uint2,
                         .offset =
                             static_cast<std::uint32_t>(offsetof(render::QuadVertex, texture))},
};

struct UpscalePush {
  glm::mat4 projection;
};

struct SharpenPush {
  glm::ivec2 lastPixel;
  std::uint32_t scene;
  float amount;
};

// How much of the sharpen its limits allow: enough to undo the upscale's softness without making
// the source's own compression stand out.
constexpr float SharpenAmount = 0.8F;

} // namespace

void MovieSharpen::draw(render::DrawContext& context) {
  const rhi::TextureId scene = context.sceneColor();
  if (context.pass() != render::Pass::Post || !scene.valid()) {
    return;
  }
  const auto width = static_cast<std::int32_t>(context.extent().width);
  const auto height = static_cast<std::int32_t>(context.extent().height);
  rhi::CommandList& commands = context.commands();
  commands.bindPipeline(pipeline_);
  commands.pushConstants(SharpenPush{.lastPixel = {std::max(width - 1, 0), std::max(height - 1, 0)},
                                     .scene = scene.index,
                                     .amount = SharpenAmount});
  commands.draw(3);
}

core::Result<MovieDrawer> MovieDrawer::create(render::Renderer& renderer) {
  auto stretched = render::QuadBatch::create(renderer, rhi::BlendMode::Opaque);
  if (!stretched) {
    return std::unexpected{stretched.error()};
  }
  auto upscale = renderer.createPipeline(
      {.spirv = shaders::MovieUpscale,
       .vertex = {.stride = sizeof(render::QuadVertex), .attributes = QuadAttributes},
       .blend = rhi::BlendMode::Opaque,
       .name = "movie upscale"},
      render::Pass::Scene);
  if (!upscale) {
    return std::unexpected{upscale.error()};
  }
  auto sharpen = renderer.createPipeline({.spirv = shaders::MovieSharpen,
                                          .vertex = {},
                                          .blend = rhi::BlendMode::Opaque,
                                          .name = "movie sharpen"},
                                         render::Pass::Post);
  if (!sharpen) {
    return std::unexpected{sharpen.error()};
  }
  return MovieDrawer{std::move(*stretched), std::move(*upscale), MovieSharpen{std::move(*sharpen)}};
}

void MovieDrawer::add(const render::Quad& quad) {
  stretched_.add(quad);
  render::appendQuad(quad, vertices_);
}

void MovieDrawer::clear() {
  stretched_.clear();
  vertices_.clear();
}

render::Drawer* MovieDrawer::post() {
  return upscaled_ && !vertices_.empty() ? &sharpen_ : nullptr;
}

void MovieDrawer::draw(render::DrawContext& context) {
  if (!upscaled_ || context.pass() != render::Pass::Scene) {
    stretched_.draw(context);
    return;
  }
  if (vertices_.empty()) {
    return;
  }
  const auto vertices = context.upload().upload(std::span<const render::QuadVertex>{vertices_});
  if (!vertices) {
    return;
  }
  rhi::CommandList& commands = context.commands();
  commands.bindPipeline(upscale_);
  commands.pushConstants(UpscalePush{.projection = render::pixelProjection(context.extent())});
  commands.bindVertexBuffer(*vertices);
  commands.draw(static_cast<std::uint32_t>(vertices_.size()));
}

} // namespace dwe::app
