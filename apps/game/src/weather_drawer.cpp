#include "weather_drawer.hpp"

#include <dwe/core/log.hpp>
#include <dwe/shaders/weather.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <utility>

namespace dwe::app {

namespace {

constexpr std::array Attributes{
    rhi::VertexAttribute{.location = 0,
                         .format = rhi::VertexFormat::Float4,
                         .offset =
                             static_cast<std::uint32_t>(offsetof(WeatherDrawer::Vertex, position))},
    rhi::VertexAttribute{.location = 1,
                         .format = rhi::VertexFormat::Float2,
                         .offset = static_cast<std::uint32_t>(offsetof(WeatherDrawer::Vertex, uv))},
    rhi::VertexAttribute{.location = 2,
                         .format = rhi::VertexFormat::Float4,
                         .offset =
                             static_cast<std::uint32_t>(offsetof(WeatherDrawer::Vertex, color))},
    rhi::VertexAttribute{.location = 3,
                         .format = rhi::VertexFormat::Uint2,
                         .offset =
                             static_cast<std::uint32_t>(offsetof(WeatherDrawer::Vertex, info))},
    rhi::VertexAttribute{.location = 4,
                         .format = rhi::VertexFormat::Float4,
                         .offset =
                             static_cast<std::uint32_t>(offsetof(WeatherDrawer::Vertex, shadow))},
};

// The cloud picture's side in texels.
constexpr std::uint32_t CloudSide = 128;

} // namespace

core::Result<std::unique_ptr<WeatherDrawer>> WeatherDrawer::create(render::Renderer& renderer) {
  const auto make = [&](rhi::BlendMode blend) {
    return renderer.createPipeline(
        {.spirv = shaders::Weather,
         .vertex = {.stride = sizeof(Vertex), .attributes = Attributes},
         .depth = {.test = false, .write = false, .compare = rhi::CompareOp::Always},
         .blend = blend,
         .name = "weather"},
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
  std::optional<rhi::TextureId> clouds;
  const std::vector<std::byte> pixels = cloudNoisePixels(CloudSide);
  if (auto made = renderer.queueTexture({.width = CloudSide, .height = CloudSide}, pixels)) {
    clouds = *made;
  } else {
    core::logWarning("weather: no cloud picture: {}", made.error().message);
  }
  return std::unique_ptr<WeatherDrawer>{
      new WeatherDrawer{renderer, std::move(*alpha), std::move(*additive), clouds}};
}

WeatherDrawer::~WeatherDrawer() {
  if (clouds_) {
    renderer_->destroyTexture(*clouds_);
  }
}

std::vector<WeatherDrawer::Vertex>
WeatherDrawer::triangles(const std::vector<WeatherQuad>& quads, glm::vec2 extent,
                         std::uint32_t clouds,
                         std::vector<std::pair<WeatherBlendMode, std::uint32_t>>* runs) {
  std::vector<Vertex> out;
  out.reserve(quads.size() * 6);
  if (extent.x <= 0.0F || extent.y <= 0.0F) {
    return out;
  }
  for (const WeatherQuad& quad : quads) {
    std::array<Vertex, 4> corners{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
      const glm::vec2 ndc = (quad.corners.at(i) / extent * 2.0F) - 1.0F;
      corners.at(i) = {.position = {ndc, 0.0F, 1.0F},
                       .uv = quad.uv.at(i),
                       .color = quad.color,
                       .info = {clouds, static_cast<std::uint32_t>(quad.shape)},
                       .shadow = quad.shadow};
    }
    if (runs != nullptr && (runs->empty() || runs->back().first != quad.blend)) {
      runs->emplace_back(quad.blend, 0U);
    }
    out.insert(out.end(), {corners[0], corners[1], corners[2], corners[2], corners[1], corners[3]});
    if (runs != nullptr) {
      runs->back().second += 6U;
    }
  }
  return out;
}

void WeatherDrawer::draw(render::DrawContext& context) {
  if (context.pass() != render::Pass::Scene || quads_.empty()) {
    return;
  }
  const glm::vec2 extent{static_cast<float>(context.extent().width),
                         static_cast<float>(context.extent().height)};
  std::vector<std::pair<WeatherBlendMode, std::uint32_t>> runs;
  const std::vector<Vertex> vertices =
      triangles(quads_, extent, clouds_ ? clouds_->index : 0U, &runs);
  if (vertices.empty()) {
    return;
  }
  const auto upload = context.upload().upload(std::span<const Vertex>{vertices});
  if (!upload) {
    return;
  }
  rhi::CommandList& commands = context.commands();
  const glm::vec2 from = glm::clamp(glm::floor(clipOrigin_), glm::vec2{0.0F}, extent);
  const glm::vec2 to = glm::clamp(glm::ceil(clipOrigin_ + clipSize_), from, extent);
  commands.setScissor({.x = static_cast<std::int32_t>(from.x),
                       .y = static_cast<std::int32_t>(from.y),
                       .width = static_cast<std::uint32_t>(to.x - from.x),
                       .height = static_cast<std::uint32_t>(to.y - from.y)});
  commands.bindVertexBuffer(*upload);
  std::uint32_t first = 0;
  for (const auto& [blend, count] : runs) {
    commands.bindPipeline(blend == WeatherBlendMode::Additive ? additive_ : alpha_);
    commands.draw(count, first);
    first += count;
  }
  // The drawers after this one start from the whole target.
  commands.setScissor(
      {.x = 0, .y = 0, .width = context.extent().width, .height = context.extent().height});
}

} // namespace dwe::app
