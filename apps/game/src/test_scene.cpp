#include "test_scene.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/render/camera.hpp>
#include <dwe/shaders/test_scene.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace dwe::app {
namespace {

struct Vertex {
  glm::vec3 position;
  glm::vec2 uv;
  glm::vec4 color;
};

constexpr std::array VertexAttributes{
    rhi::VertexAttribute{.location = 0,
                         .format = rhi::VertexFormat::Float3,
                         .offset = static_cast<std::uint32_t>(offsetof(Vertex, position))},
    rhi::VertexAttribute{.location = 1,
                         .format = rhi::VertexFormat::Float2,
                         .offset = static_cast<std::uint32_t>(offsetof(Vertex, uv))},
    rhi::VertexAttribute{.location = 2,
                         .format = rhi::VertexFormat::Float4,
                         .offset = static_cast<std::uint32_t>(offsetof(Vertex, color))},
};

// Matches Push in shaders/test_scene.slang (std430: the pointer first, then the indices).
struct MeshPush {
  std::uint64_t transforms;
  std::uint32_t transform;
  std::uint32_t texture;
  std::uint32_t sampler;
  std::uint32_t padding;
};

constexpr std::uint32_t CubeIndexCount = 36;
constexpr std::uint32_t FloorIndexCount = 6;
constexpr std::uint32_t CheckerSide = 64;
constexpr std::uint32_t MinimapSide = 32;
// The two triangles of a quad whose corners go round in order.
constexpr std::array<std::uint16_t, 6> QuadIndices{0, 1, 2, 0, 2, 3};

// A unit cube around the origin, one color per face, then a floor quad at y = 0 that cuts through
// it.
std::pair<std::vector<Vertex>, std::vector<std::uint16_t>> buildGeometry() {
  std::vector<Vertex> vertices;
  std::vector<std::uint16_t> indices;
  const std::array<glm::vec3, 6> normals{glm::vec3{1, 0, 0}, glm::vec3{-1, 0, 0},
                                         glm::vec3{0, 1, 0}, glm::vec3{0, -1, 0},
                                         glm::vec3{0, 0, 1}, glm::vec3{0, 0, -1}};
  const std::array<glm::vec4, 6> colors{
      glm::vec4{1.0F, 0.4F, 0.4F, 1.0F}, glm::vec4{0.4F, 1.0F, 0.4F, 1.0F},
      glm::vec4{0.4F, 0.4F, 1.0F, 1.0F}, glm::vec4{1.0F, 1.0F, 0.4F, 1.0F},
      glm::vec4{1.0F, 0.4F, 1.0F, 1.0F}, glm::vec4{0.4F, 1.0F, 1.0F, 1.0F}};
  for (std::size_t face = 0; face < normals.size(); ++face) {
    const glm::vec3 normal = normals[face];
    // Two axes across the face.
    const glm::vec3 u = std::abs(normal.y) > 0.5F ? glm::vec3{1, 0, 0} : glm::vec3{0, 1, 0};
    const glm::vec3 v = glm::cross(normal, u);
    const auto base = static_cast<std::uint16_t>(vertices.size());
    const std::array<glm::vec2, 4> corners{glm::vec2{-1, -1}, glm::vec2{1, -1}, glm::vec2{1, 1},
                                           glm::vec2{-1, 1}};
    for (const glm::vec2 corner : corners) {
      vertices.push_back({.position = 0.5F * (normal + (corner.x * u) + (corner.y * v)),
                          .uv = (corner + 1.0F) * 0.5F,
                          .color = colors[face]});
    }
    for (const std::uint16_t offset : QuadIndices) {
      indices.push_back(static_cast<std::uint16_t>(base + offset));
    }
  }
  const auto floor = static_cast<std::uint16_t>(vertices.size());
  constexpr float FloorHalf = 3.0F;
  constexpr float FloorRepeats = 6.0F;
  for (const glm::vec2 corner :
       {glm::vec2{-1, -1}, glm::vec2{1, -1}, glm::vec2{1, 1}, glm::vec2{-1, 1}}) {
    vertices.push_back({.position = {corner.x * FloorHalf, 0.0F, corner.y * FloorHalf},
                        .uv = (corner + 1.0F) * 0.5F * FloorRepeats,
                        .color = glm::vec4{1.0F}});
  }
  for (const std::uint16_t offset : QuadIndices) {
    indices.push_back(static_cast<std::uint16_t>(floor + offset));
  }
  return {std::move(vertices), std::move(indices)};
}

std::vector<std::byte> checkerPixels() {
  std::vector<std::byte> pixels;
  pixels.reserve(std::size_t{CheckerSide} * CheckerSide * 4);
  for (std::uint32_t y = 0; y < CheckerSide; ++y) {
    for (std::uint32_t x = 0; x < CheckerSide; ++x) {
      const bool light = ((x / 8) + (y / 8)) % 2 == 0;
      const auto value = static_cast<std::byte>(light ? 220 : 60);
      pixels.insert(pixels.end(),
                    {value, value, static_cast<std::byte>(light ? 220 : 90), std::byte{255}});
    }
  }
  return pixels;
}

} // namespace

core::Result<std::unique_ptr<TestScene>> TestScene::create(render::Renderer& renderer,
                                                           bool textureChurn) {
  auto quads = render::QuadBatch::create(renderer);
  if (!quads) {
    return std::unexpected{quads.error()};
  }
  std::unique_ptr<TestScene> scene{new TestScene{renderer, std::move(*quads), textureChurn}};

  auto pipeline =
      renderer.createPipeline({.spirv = shaders::TestScene,
                               .vertex = {.stride = sizeof(Vertex), .attributes = VertexAttributes},
                               .depth = {.test = true,
                                         .write = true,
                                         .compare = rhi::CompareOp::LessOrEqual,
                                         .bias = false},
                               .name = "test scene"},
                              render::Pass::Scene);
  if (!pipeline) {
    return std::unexpected{pipeline.error()};
  }
  scene->meshes_.pipeline = std::move(*pipeline);

  const auto [vertices, indices] = buildGeometry();
  auto vertexBuffer = rhi::Buffer::createWithData(renderer.device(), rhi::BufferUsage::Vertex,
                                                  std::as_bytes(std::span{vertices}));
  auto indexBuffer = rhi::Buffer::createWithData(renderer.device(), rhi::BufferUsage::Index,
                                                 std::as_bytes(std::span{indices}));
  if (!vertexBuffer || !indexBuffer) {
    return core::fail("could not upload the test scene's meshes");
  }
  scene->meshes_.vertices = std::move(*vertexBuffer);
  scene->meshes_.indices = std::move(*indexBuffer);

  const std::vector<std::byte> checker = checkerPixels();
  auto checkerTexture =
      renderer.createTexture({.width = CheckerSide, .height = CheckerSide}, checker);
  if (!checkerTexture) {
    return std::unexpected{checkerTexture.error()};
  }
  scene->meshes_.checker = *checkerTexture;

  auto minimap = renderer.createTexture({.width = MinimapSide, .height = MinimapSide});
  if (!minimap) {
    return std::unexpected{minimap.error()};
  }
  scene->minimap_ = *minimap;
  return scene;
}

TestScene::TestScene(render::Renderer& renderer, render::QuadBatch quads, bool textureChurn)
    : renderer_(&renderer), quads_(std::move(quads)), textureChurn_(textureChurn) {}

TestScene::~TestScene() {
  for (const render::TextureId texture : {meshes_.checker, minimap_, churned_}) {
    if (texture.valid()) {
      renderer_->destroyTexture(texture);
    }
  }
}

void TestScene::churnTexture() {
  // A new texture each frame, in a colour that cycles, replacing the last one while frames that
  // drew it may still be in flight.
  constexpr std::uint32_t Side = 16;
  const std::byte shade{static_cast<unsigned char>(churnCount_ * 7U)};
  std::vector<std::byte> pixels(std::size_t{Side} * Side * 4, std::byte{255});
  for (std::size_t texel = 0; texel < std::size_t{Side} * Side; ++texel) {
    pixels[texel * 4] = shade;
  }
  auto texture = renderer_->queueTexture({.width = Side, .height = Side}, pixels);
  if (!texture) {
    core::logError("{}", texture.error().message);
    return;
  }
  const render::TextureId old = churned_;
  churned_ = *texture;
  if (old.valid()) {
    renderer_->destroyTexture(old);
    if (churnCount_ == 1) {
      // A stale id: ignored with a warning, without touching the texture that may now reuse its
      // index.
      renderer_->destroyTexture(old);
    }
  }
  ++churnCount_;
}

void TestScene::update(double seconds) {
  meshes_.angle = static_cast<float>(seconds);
  if (textureChurn_) {
    churnTexture();
  }

  // A dot that walks around the minimap's border, one texel per step: a partial texture update.
  constexpr std::int32_t Perimeter = 4 * (MinimapSide - 1);
  const auto dot = static_cast<std::int32_t>(seconds * 20.0) % Perimeter;
  if (dot != minimapDot_) {
    minimapDot_ = dot;
    std::vector<std::byte> pixels(std::size_t{MinimapSide} * MinimapSide * 4, std::byte{0});
    for (std::size_t texel = 0; texel < std::size_t{MinimapSide} * MinimapSide; ++texel) {
      pixels[(texel * 4) + 2] = std::byte{120};
      pixels[(texel * 4) + 3] = std::byte{200};
    }
    const std::int32_t side = MinimapSide - 1;
    const std::int32_t edge = dot / side;
    const std::int32_t step = dot % side;
    const std::array<std::int32_t, 4> xs{step, side, side - step, 0};
    const std::array<std::int32_t, 4> ys{0, step, side, side - step};
    const auto x = static_cast<std::size_t>(xs[static_cast<std::size_t>(edge)]);
    const auto y = static_cast<std::size_t>(ys[static_cast<std::size_t>(edge)]);
    const std::size_t texel = (y * MinimapSide) + x;
    for (std::size_t channel = 0; channel < 4; ++channel) {
      pixels[(texel * 4) + channel] = std::byte{255};
    }
    if (auto updated = renderer_->queueTextureUpdate(
            minimap_, {.x = 0, .y = 0, .width = MinimapSide, .height = MinimapSide}, pixels);
        !updated) {
      core::logError("{}", updated.error().message);
    }
  }

  const rhi::Extent extent = renderer_->targetExtent();
  const auto right = static_cast<float>(extent.width);
  quads_.clear();
  quads_.add({.position = {right - 148.0F, 20.0F},
              .size = {128.0F, 128.0F},
              .texture = meshes_.checker,
              .sampler = rhi::SamplerKind::NearestClamp});
  quads_.add({.position = {right - 148.0F, 168.0F},
              .size = {128.0F, 128.0F},
              .texture = minimap_,
              .sampler = rhi::SamplerKind::NearestClamp});
  quads_.add({.position = {right - 156.0F, 312.0F},
              .size = {144.0F, 24.0F},
              .color = {1.0F, 0.6F, 0.1F, 0.6F},
              .texture = renderer_->whiteTexture()});
  if (churned_.valid()) {
    quads_.add({.position = {right - 156.0F, 344.0F}, .size = {24.0F, 24.0F}, .texture = churned_});
  }
}

void TestScene::Meshes::draw(render::DrawContext& context) {
  const rhi::Extent extent = context.extent();
  if (!pipeline || !vertices || !indices || extent.width == 0 || extent.height == 0) {
    return;
  }
  const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
  const render::Camera camera{
      .view =
          glm::lookAt(glm::vec3{std::sin(angle * 0.3F) * 4.0F, 2.0F, std::cos(angle * 0.3F) * 4.0F},
                      glm::vec3{0.0F, 0.0F, 0.0F}, glm::vec3{0.0F, 1.0F, 0.0F}),
      .projection = render::perspective(glm::radians(50.0F), aspect, 0.1F, 100.0F),
      .viewport = render::fullViewport(extent)};
  const glm::mat4 cube = glm::rotate(glm::translate(glm::mat4{1.0F}, {0.0F, 0.25F, 0.0F}), angle,
                                     glm::normalize(glm::vec3{1.0F, 1.0F, 0.0F}));
  const std::array transforms{camera.viewProjection() * cube, camera.viewProjection()};
  const auto uploaded = context.upload().upload(std::span<const glm::mat4>{transforms});
  if (!uploaded) {
    return;
  }

  context.commands().bindPipeline(*pipeline);
  context.commands().setViewport(camera.viewport);
  context.commands().bindVertexBuffer(*vertices);
  context.commands().bindIndexBuffer(*indices, rhi::IndexType::Uint16);
  context.commands().pushConstants(
      MeshPush{.transforms = uploaded->address,
               .transform = 0,
               .texture = checker.index,
               .sampler = static_cast<std::uint32_t>(rhi::SamplerKind::LinearRepeat),
               .padding = 0});
  context.commands().drawIndexed(CubeIndexCount);
  context.commands().pushConstants(
      MeshPush{.transforms = uploaded->address,
               .transform = 1,
               .texture = checker.index,
               .sampler = static_cast<std::uint32_t>(rhi::SamplerKind::NearestRepeat),
               .padding = 0});
  context.commands().drawIndexed(FloorIndexCount, CubeIndexCount);
}

} // namespace dwe::app
