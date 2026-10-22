#include <dwe/render/camera.hpp>
#include <dwe/render/quad_batch.hpp>
#include <dwe/shaders/quad.hpp>

#include <array>
#include <cstddef>
#include <utility>

namespace dwe::render {
namespace {

constexpr std::array QuadAttributes{
    rhi::VertexAttribute{.location = 0,
                         .format = rhi::VertexFormat::Float2,
                         .offset = static_cast<std::uint32_t>(offsetof(QuadVertex, position))},
    rhi::VertexAttribute{.location = 1,
                         .format = rhi::VertexFormat::Float2,
                         .offset = static_cast<std::uint32_t>(offsetof(QuadVertex, uv))},
    rhi::VertexAttribute{.location = 2,
                         .format = rhi::VertexFormat::Float4,
                         .offset = static_cast<std::uint32_t>(offsetof(QuadVertex, color))},
    rhi::VertexAttribute{.location = 3,
                         .format = rhi::VertexFormat::Uint2,
                         .offset = static_cast<std::uint32_t>(offsetof(QuadVertex, texture))},
};

struct QuadPush {
  glm::mat4 projection;
};

} // namespace

void appendQuad(const Quad& quad, std::vector<QuadVertex>& vertices) {
  const glm::uvec2 texture{quad.texture.index, static_cast<std::uint32_t>(quad.sampler)};
  const glm::vec2 min = quad.position;
  const glm::vec2 max = quad.position + quad.size;
  const QuadVertex topLeft{
      .position = min, .uv = quad.uvMin, .color = quad.color, .texture = texture};
  const QuadVertex topRight{.position = {max.x, min.y},
                            .uv = {quad.uvMax.x, quad.uvMin.y},
                            .color = quad.color,
                            .texture = texture};
  const QuadVertex bottomLeft{.position = {min.x, max.y},
                              .uv = {quad.uvMin.x, quad.uvMax.y},
                              .color = quad.color,
                              .texture = texture};
  const QuadVertex bottomRight{
      .position = max, .uv = quad.uvMax, .color = quad.color, .texture = texture};
  vertices.insert(vertices.end(),
                  {topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight});
}

core::Result<QuadBatch> QuadBatch::create(Renderer& renderer, rhi::BlendMode blend) {
  const rhi::GraphicsPipelineDesc desc{
      .spirv = shaders::Quad,
      .vertex = {.stride = sizeof(QuadVertex), .attributes = QuadAttributes},
      .blend = blend,
      .name = "quads",
  };
  auto scene = renderer.createPipeline(desc, Pass::Scene);
  if (!scene) {
    return std::unexpected{scene.error()};
  }
  auto ui = renderer.createPipeline(desc, Pass::Ui);
  if (!ui) {
    return std::unexpected{ui.error()};
  }
  return QuadBatch{std::move(*scene), std::move(*ui)};
}

QuadBatch::QuadBatch(rhi::GraphicsPipeline scene, rhi::GraphicsPipeline ui)
    : scene_(std::move(scene)), ui_(std::move(ui)) {}

void QuadBatch::draw(DrawContext& context) {
  if (vertices_.empty()) {
    return;
  }
  const auto vertices = context.upload().upload(std::span<const QuadVertex>{vertices_});
  if (!vertices) {
    return;
  }
  context.commands().bindPipeline(context.pass() == Pass::Scene ? scene_ : ui_);
  context.commands().pushConstants(QuadPush{.projection = pixelProjection(context.extent())});
  context.commands().bindVertexBuffer(*vertices);
  context.commands().draw(static_cast<std::uint32_t>(vertices_.size()));
}

} // namespace dwe::render
