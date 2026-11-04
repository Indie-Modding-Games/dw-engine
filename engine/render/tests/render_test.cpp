#include <dwe/render/camera.hpp>
#include <dwe/render/hd_scenery_wrap.hpp>
#include <dwe/render/quad_batch.hpp>
#include <dwe/render/screen_fade.hpp>
#include <dwe/render/upload_budget.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <vector>

using namespace dwe;

namespace {

glm::vec4 project(const glm::mat4& matrix, glm::vec3 point) {
  const glm::vec4 clip = matrix * glm::vec4{point, 1.0F};
  return clip / clip.w;
}

} // namespace

TEST_CASE("the pixel projection maps the target's corners to clip space, Y down") {
  const glm::mat4 projection = render::pixelProjection({.width = 640, .height = 480});
  const glm::vec4 topLeft = project(projection, {0.0F, 0.0F, 0.0F});
  const glm::vec4 bottomRight = project(projection, {640.0F, 480.0F, 0.0F});
  const glm::vec4 center = project(projection, {320.0F, 240.0F, 0.0F});
  CHECK(topLeft.x == doctest::Approx(-1.0));
  CHECK(topLeft.y == doctest::Approx(-1.0));
  CHECK(bottomRight.x == doctest::Approx(1.0));
  CHECK(bottomRight.y == doctest::Approx(1.0));
  CHECK(center.x == doctest::Approx(0.0));
  CHECK(center.y == doctest::Approx(0.0));
}

TEST_CASE("the perspective projection puts up on top of the screen and depth from 0 to 1") {
  const glm::mat4 projection = render::perspective(glm::radians(60.0F), 16.0F / 9.0F, 1.0F, 100.0F);
  const glm::vec4 near = project(projection, {0.0F, 0.0F, -1.0F});
  const glm::vec4 far = project(projection, {0.0F, 0.0F, -100.0F});
  const glm::vec4 above = project(projection, {0.0F, 1.0F, -10.0F});
  CHECK(near.z == doctest::Approx(0.0));
  CHECK(far.z == doctest::Approx(1.0));
  // Vulkan's clip space has Y down: a point above the axis lands in the upper half, at negative Y.
  CHECK(above.y < 0.0F);
}

TEST_CASE("a quad becomes two triangles covering its rectangle") {
  std::vector<render::QuadVertex> vertices;
  render::appendQuad({.position = {10.0F, 20.0F},
                      .size = {30.0F, 40.0F},
                      .uvMin = {0.0F, 0.5F},
                      .uvMax = {0.5F, 1.0F},
                      .color = {1.0F, 0.5F, 0.25F, 1.0F},
                      .texture = {.index = 7},
                      .sampler = rhi::SamplerKind::NearestRepeat},
                     vertices);
  REQUIRE(vertices.size() == 6);
  glm::vec2 min{1e9F};
  glm::vec2 max{-1e9F};
  for (const render::QuadVertex& vertex : vertices) {
    min = glm::min(min, vertex.position);
    max = glm::max(max, vertex.position);
    CHECK(vertex.texture == glm::uvec2{7, 1});
    CHECK(vertex.color == glm::vec4{1.0F, 0.5F, 0.25F, 1.0F});
    // The UV follows the position across the rectangle.
    const glm::vec2 along = (vertex.position - glm::vec2{10.0F, 20.0F}) / glm::vec2{30.0F, 40.0F};
    CHECK(vertex.uv.x == doctest::Approx(along.x * 0.5F));
    CHECK(vertex.uv.y == doctest::Approx(0.5F + (along.y * 0.5F)));
  }
  CHECK(min == glm::vec2{10.0F, 20.0F});
  CHECK(max == glm::vec2{40.0F, 60.0F});

  // Both triangles wind the same way.
  const auto winding = [&](std::size_t first) {
    const glm::vec2 a = vertices[first + 1].position - vertices[first].position;
    const glm::vec2 b = vertices[first + 2].position - vertices[first].position;
    return (a.x * b.y) - (a.y * b.x);
  };
  CHECK(winding(0) * winding(3) > 0.0F);
}

TEST_CASE("the screen fade subtracts a grey as dark as asked, opaque so all of it comes off") {
  CHECK(render::screenFadeColor(0.0F) == glm::vec4{0.0F, 0.0F, 0.0F, 1.0F});
  CHECK(render::screenFadeColor(0.25F) == glm::vec4{0.25F, 0.25F, 0.25F, 1.0F});
  CHECK(render::screenFadeColor(2.0F) == glm::vec4{1.0F});
  CHECK(render::screenFadeColor(-1.0F) == glm::vec4{0.0F, 0.0F, 0.0F, 1.0F});
}

TEST_CASE("queued texture copies leave the passes a share of the frame's upload memory") {
  constexpr std::size_t MiB = std::size_t{1} << 20U;
  constexpr std::size_t PerFrame = 16 * MiB;
  constexpr std::size_t Reserve = PerFrame / render::DrawUploadShare;
  // A map's first frame: its background (5 MiB), the HD scenery's colours (5 MiB), sprite sheets
  // and models. The copies stop where the reserve starts; the rest waits for the next frame.
  std::size_t used = 64 * 1024; // what the drawers' prepare uploaded
  bool first = true;
  std::size_t waiting = 0;
  for (const std::size_t bytes : {5 * MiB, 5 * MiB, 2 * MiB, 3 * MiB, 1 * MiB}) {
    if (render::textureCopyFits(used, bytes, PerFrame, Reserve, first)) {
      used += bytes;
      first = false;
    } else {
      waiting += bytes;
    }
  }
  CHECK(used + Reserve <= PerFrame);
  CHECK(waiting > 0);
  // The map's vertices (the frame that once had 39 KB left for 42 KB) still fit.
  CHECK(used + (42 * 1024) <= PerFrame);
}

TEST_CASE("a texture copy larger than the reserve leaves goes alone, first in a frame") {
  constexpr std::size_t MiB = std::size_t{1} << 20U;
  constexpr std::size_t PerFrame = 16 * MiB;
  constexpr std::size_t Reserve = PerFrame / render::DrawUploadShare;
  CHECK(render::textureCopyFits(0, 14 * MiB, PerFrame, Reserve, true));
  CHECK_FALSE(render::textureCopyFits(1 * MiB, 14 * MiB, PerFrame, Reserve, false));
  CHECK_FALSE(render::textureCopyFits(3 * MiB, 14 * MiB, PerFrame, Reserve, true));
  CHECK(render::textureCopyFits(0, 12 * MiB, PerFrame, Reserve, true));
  CHECK_FALSE(render::textureCopyFits(1, 12 * MiB, PerFrame, Reserve, true));
}

TEST_CASE("HD scenery filters the whole window first, then only what the scroll uncovered") {
  const glm::ivec2 size{320, 240};
  using Regions = std::vector<glm::ivec4>;
  CHECK(render::hdUncoveredRegions(size, {5, 5}, false) == Regions{{0, 0, 320, 240}});
  CHECK(render::hdUncoveredRegions(size, {0, 0}, true).empty());
  CHECK(render::hdUncoveredRegions(size, {3, 0}, true) == Regions{{317, 0, 320, 240}});
  CHECK(render::hdUncoveredRegions(size, {-2, 4}, true) ==
        Regions{{0, 0, 2, 240}, {0, 236, 320, 240}});
  CHECK(render::hdUncoveredRegions(size, {0, -240}, true) == Regions{{0, 0, 320, 240}});
}

TEST_CASE("HD scenery's wrapped target shows every window pixel's own background pixel") {
  // A small window; each target pixel remembers which pixel of the scaled background it holds.
  constexpr int Width = 7;
  constexpr int Height = 5;
  std::array<glm::ivec2, std::size_t{Width} * Height> target{};
  const glm::ivec2 size{Width, Height};
  const auto fill = [&](glm::ivec2 shift, glm::ivec2 moved, bool filled) {
    for (const glm::ivec4& region : render::hdUncoveredRegions(size, moved, filled)) {
      for (const glm::ivec3& across :
           render::hdWrappedSpan(region.x + shift.x, region.z + shift.x, Width)) {
        for (const glm::ivec3& down :
             render::hdWrappedSpan(region.y + shift.y, region.w + shift.y, Height)) {
          for (int y = 0; y < down.z; ++y) {
            for (int x = 0; x < across.z; ++x) {
              target[static_cast<std::size_t>(((down.y + y) * Width) + across.y + x)] = {
                  across.x + x, down.x + y};
            }
          }
        }
      }
    }
  };
  glm::ivec2 shift{-3, 2};
  fill(shift, {}, false);
  for (const glm::ivec2 next : {glm::ivec2{-1, 2}, glm::ivec2{4, 1}, glm::ivec2{4, -6},
                                glm::ivec2{-2, -5}, glm::ivec2{12, 9}, glm::ivec2{11, 9}}) {
    fill(next, next - shift, true);
    shift = next;
    const glm::ivec2 wrap = ((shift % size) + size) % size;
    for (int y = 0; y < Height; ++y) {
      for (int x = 0; x < Width; ++x) {
        const glm::ivec2 held = (glm::ivec2{x, y} + wrap) % size;
        CHECK(target[static_cast<std::size_t>((held.y * Width) + held.x)] ==
              glm::ivec2{x, y} + shift);
      }
    }
  }
}
