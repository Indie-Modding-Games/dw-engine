#pragma once

#include <dwe/rhi/bindless.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace dwe::app {

// One primitive of a model's object as the original's TMD holds it (a triangle or a quad of one
// bone), posed in the world: what the scenes break into shards and draw as wireframe (evl.c:
// EVL_buildShardSet, model.c: renderWireframed).
struct BodyFace {
  std::uint32_t bone = 0;
  // Its place among its bone's primitives (renderWireframed picks the drawn ones by it).
  std::uint32_t index = 0;
  std::uint8_t count = 3;
  // The corners in the original's order: a quad's 0 1 2 and 1 2 3 are its triangles (POLY_FT4,
  // with 3 opposite 0), clockwise seen from the front.
  std::array<glm::vec3, 4> corners{};
  std::array<glm::vec2, 4> uvs{};
  // The mean of the first three corners' unit normals, in world space (EVL_buildShardSet's
  // "centre", which drives the shard).
  glm::vec3 normal{0.0F};
  // The face's texture; the renderer's white texture for a textureless one.
  rhi::TextureId texture{};
};

} // namespace dwe::app
