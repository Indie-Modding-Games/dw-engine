#include <dwe/render/camera.hpp>
#include <dwe/shaders/rml.hpp>
#include <dwe/ui/ui_renderer.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "ui_renderer_impl.hpp"

namespace dwe::ui {
namespace {

constexpr std::array RmlAttributes{
    rhi::VertexAttribute{.location = 0,
                         .format = rhi::VertexFormat::Float2,
                         .offset = static_cast<std::uint32_t>(offsetof(Rml::Vertex, position))},
    rhi::VertexAttribute{.location = 1,
                         .format = rhi::VertexFormat::Unorm8x4,
                         .offset = static_cast<std::uint32_t>(offsetof(Rml::Vertex, colour))},
    rhi::VertexAttribute{.location = 2,
                         .format = rhi::VertexFormat::Float2,
                         .offset = static_cast<std::uint32_t>(offsetof(Rml::Vertex, tex_coord))},
};

// Mirrors Push in rml.slang.
struct RmlPush {
  glm::mat4 transform;
  glm::vec2 translation;
  std::uint32_t texture;
  std::uint32_t sharp;
};

// A scissor rectangle kept inside the target, as Vulkan requires.
rhi::Rect clampToTarget(const rhi::Rect& rect, rhi::Extent extent) {
  const auto width = static_cast<std::int64_t>(extent.width);
  const auto height = static_cast<std::int64_t>(extent.height);
  const std::int64_t left = std::clamp<std::int64_t>(rect.x, 0, width);
  const std::int64_t top = std::clamp<std::int64_t>(rect.y, 0, height);
  const std::int64_t right =
      std::clamp<std::int64_t>(std::int64_t{rect.x} + rect.width, left, width);
  const std::int64_t bottom =
      std::clamp<std::int64_t>(std::int64_t{rect.y} + rect.height, top, height);
  return {.x = static_cast<std::int32_t>(left),
          .y = static_cast<std::int32_t>(top),
          .width = static_cast<std::uint32_t>(right - left),
          .height = static_cast<std::uint32_t>(bottom - top)};
}

} // namespace

core::Result<std::unique_ptr<UiRenderer>> UiRenderer::create(render::Renderer& renderer) {
  auto pipeline = renderer.createPipeline(
      {
          .spirv = shaders::Rml,
          .vertex = {.stride = sizeof(Rml::Vertex), .attributes = RmlAttributes},
          .blend = rhi::BlendMode::PremultipliedAlpha,
          .name = "ui",
      },
      render::Pass::Ui);
  if (!pipeline) {
    return std::unexpected{pipeline.error()};
  }
  auto impl = std::make_unique<Impl>(
      Impl{.renderer = &renderer, .pipeline = std::move(*pipeline), .draws = {}});
  return std::unique_ptr<UiRenderer>{new UiRenderer{std::move(impl)}};
}

UiRenderer::UiRenderer(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}

UiRenderer::~UiRenderer() = default;

std::size_t UiRenderer::drawCount() const {
  return impl_->draws.size();
}

void UiRenderer::draw(render::DrawContext& context) {
  if (impl_->draws.empty()) {
    return;
  }
  rhi::CommandList& commands = context.commands();
  const rhi::Extent extent = context.extent();
  const glm::mat4 projection = render::pixelProjection(extent);
  const rhi::Rect whole{.x = 0, .y = 0, .width = extent.width, .height = extent.height};
  commands.bindPipeline(impl_->pipeline);
  for (const UiDraw& draw : impl_->draws) {
    const UiGeometry& geometry = *draw.geometry;
    const rhi::Rect scissor = draw.scissor ? clampToTarget(*draw.scissor, extent) : whole;
    if (scissor.width == 0 || scissor.height == 0) {
      continue;
    }
    commands.setScissor(scissor);
    commands.pushConstants(RmlPush{.transform = projection * draw.transform,
                                   .translation = draw.translation,
                                   .texture = draw.texture.index,
                                   .sharp = draw.sharp ? 1U : 0U});
    commands.bindVertexBuffer(geometry.vertices);
    commands.bindIndexBuffer(geometry.indices, rhi::IndexType::Uint32);
    commands.drawIndexed(geometry.indexCount);
  }
}

} // namespace dwe::ui
