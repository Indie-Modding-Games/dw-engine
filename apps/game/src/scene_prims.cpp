#include "scene_prims.hpp"

#include <dwe/shaders/scene_prims.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <utility>

namespace dwe::app {
namespace {

constexpr std::array Attributes{
    rhi::VertexAttribute{
        .location = 0,
        .format = rhi::VertexFormat::Float4,
        .offset = static_cast<std::uint32_t>(offsetof(ScenePrimDrawer::Vertex, position))},
    rhi::VertexAttribute{.location = 1,
                         .format = rhi::VertexFormat::Float2,
                         .offset =
                             static_cast<std::uint32_t>(offsetof(ScenePrimDrawer::Vertex, uv))},
    rhi::VertexAttribute{.location = 2,
                         .format = rhi::VertexFormat::Float4,
                         .offset =
                             static_cast<std::uint32_t>(offsetof(ScenePrimDrawer::Vertex, color))},
    rhi::VertexAttribute{
        .location = 3,
        .format = rhi::VertexFormat::Uint2,
        .offset = static_cast<std::uint32_t>(offsetof(ScenePrimDrawer::Vertex, texture))},
};

// Points this close to the camera plane or behind it are not drawn (the original drops primitives
// whose depth is out of its ordering table).
constexpr float MinClipW = 1.0e-3F;

// Which texels a copy of a primitive draws (scene_prims.slang): all of them, only those without
// STP (drawn opaque) or only those with it (mixed); in the bits over the sampler's.
enum class TexelFilter : std::uint32_t { All = 0, Opaque = 1, SemiTransparent = 2 };
constexpr std::uint32_t FilterShift = 8;

// A primitive with a per-texel STP mask becomes two: its opaque texels drawn with the alpha
// pipeline, then its STP texels with its own blend.
std::vector<std::pair<ScenePrim, TexelFilter>> splitByStp(const std::vector<ScenePrim>& prims) {
  std::vector<std::pair<ScenePrim, TexelFilter>> out;
  out.reserve(prims.size());
  for (const ScenePrim& prim : prims) {
    if (!prim.stpMask) {
      out.emplace_back(prim, TexelFilter::All);
      continue;
    }
    ScenePrim opaque = prim;
    opaque.blend = ScenePrimBlend::Alpha;
    out.emplace_back(opaque, TexelFilter::Opaque);
    out.emplace_back(prim, TexelFilter::SemiTransparent);
  }
  return out;
}
} // namespace

core::Result<std::unique_ptr<ScenePrimDrawer>> ScenePrimDrawer::create(render::Renderer& renderer) {
  const auto make = [&](rhi::BlendMode blend) {
    return renderer.createPipeline(
        {.spirv = shaders::ScenePrims,
         .vertex = {.stride = sizeof(Vertex), .attributes = Attributes},
         .depth = {.test = true, .write = false, .compare = rhi::CompareOp::LessOrEqual},
         .blend = blend,
         .name = "scene prims"},
        render::Pass::Scene);
  };
  auto alpha = make(rhi::BlendMode::Alpha);
  if (!alpha) {
    return std::unexpected{alpha.error()};
  }
  auto additive = make(rhi::BlendMode::Additive);
  if (!additive) {
    return std::unexpected{additive.error()};
  }
  auto subtractive = make(rhi::BlendMode::Subtractive);
  if (!subtractive) {
    return std::unexpected{subtractive.error()};
  }
  return std::unique_ptr<ScenePrimDrawer>{
      new ScenePrimDrawer{std::move(*alpha), std::move(*additive), std::move(*subtractive)}};
}

void ScenePrimDrawer::set(std::vector<ScenePrim> prims, float pixelScale) {
  prims_ = std::move(prims);
  pixelScale_ = pixelScale;
}

std::vector<ScenePrimDrawer::Vertex>
ScenePrimDrawer::triangles(const std::vector<ScenePrim>& prims, float pixelScale, glm::vec2 extent,
                           std::vector<std::pair<ScenePrimBlend, std::uint32_t>>* runs) {
  std::vector<Vertex> out;
  out.reserve(prims.size() * 6);
  // Original pixels to clip units per unit of w (clip y points down, as screen y).
  const glm::vec2 toNdc = glm::vec2{2.0F * pixelScale} / extent;
  for (const auto& [prim, filter] : splitByStp(prims)) {
    const std::size_t count = prim.shape == ScenePrim::Shape::Triangle ? 3
                              : prim.shape == ScenePrim::Shape::Quad   ? 4
                                                                       : 2;
    bool visible = true;
    for (std::size_t i = 0; i < count; ++i) {
      visible = visible && prim.corners.at(i).clip.w > MinClipW;
    }
    if (!visible) {
      continue;
    }
    const glm::uvec2 texture{prim.texture.index,
                             static_cast<std::uint32_t>(rhi::SamplerKind::NearestClamp) |
                                 (static_cast<std::uint32_t>(filter) << FilterShift)};
    const auto vertex = [&](const ScenePrimCorner& corner, glm::vec2 offset) {
      glm::vec4 position = corner.clip;
      position.x += offset.x * toNdc.x * position.w;
      position.y += offset.y * toNdc.y * position.w;
      return Vertex{.position = position,
                    .uv = corner.uv,
                    .color = prim.color * corner.shade,
                    .texture = texture};
    };
    std::array<Vertex, 4> corners{};
    if (prim.shape == ScenePrim::Shape::Line) {
      // One original pixel wide, across the line on screen; its ends moved by their offsets.
      const ScenePrimCorner& a = prim.corners[0];
      const ScenePrimCorner& b = prim.corners[1];
      const glm::vec2 from = (glm::vec2{a.clip} / a.clip.w / toNdc) + a.offset;
      const glm::vec2 to = (glm::vec2{b.clip} / b.clip.w / toNdc) + b.offset;
      const glm::vec2 along = to - from;
      const float length = glm::length(along);
      const glm::vec2 across =
          length > 0.0F ? glm::vec2{-along.y, along.x} / length * 0.5F : glm::vec2{0.0F, 0.5F};
      corners = {vertex(a, a.offset + across), vertex(a, a.offset - across),
                 vertex(b, b.offset + across), vertex(b, b.offset - across)};
    } else {
      for (std::size_t i = 0; i < count; ++i) {
        corners.at(i) = vertex(prim.corners.at(i), prim.corners.at(i).offset);
      }
    }
    if (runs != nullptr && (runs->empty() || runs->back().first != prim.blend)) {
      runs->emplace_back(prim.blend, 0U);
    }
    const std::size_t before = out.size();
    out.insert(out.end(), {corners[0], corners[1], corners[2]});
    if (count != 3) {
      out.insert(out.end(), {corners[2], corners[1], corners[3]});
    }
    if (runs != nullptr) {
      runs->back().second += static_cast<std::uint32_t>(out.size() - before);
    }
  }
  return out;
}

void ScenePrimDrawer::draw(render::DrawContext& context) {
  if (context.pass() != render::Pass::Scene || prims_.empty()) {
    return;
  }
  const glm::vec2 extent{static_cast<float>(context.extent().width),
                         static_cast<float>(context.extent().height)};
  std::vector<std::pair<ScenePrimBlend, std::uint32_t>> runs;
  const std::vector<Vertex> vertices = triangles(prims_, pixelScale_, extent, &runs);
  if (vertices.empty()) {
    return;
  }
  const auto upload = context.upload().upload(std::span<const Vertex>{vertices});
  if (!upload) {
    return;
  }
  rhi::CommandList& commands = context.commands();
  commands.bindVertexBuffer(*upload);
  std::uint32_t first = 0;
  for (const auto& [blend, count] : runs) {
    switch (blend) {
    case ScenePrimBlend::Alpha:
      commands.bindPipeline(alpha_);
      break;
    case ScenePrimBlend::Additive:
      commands.bindPipeline(additive_);
      break;
    case ScenePrimBlend::Subtractive:
      commands.bindPipeline(subtractive_);
      break;
    }
    commands.draw(count, first);
    first += count;
  }
}

} // namespace dwe::app
