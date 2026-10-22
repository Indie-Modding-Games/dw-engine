#include <dwe/rhi/image.hpp>
#include <dwe/rhi/pipeline.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>

using namespace dwe::rhi;

namespace {

// Just the SPIR-V header: validate() only looks at the magic number and the size.
constexpr std::array<std::uint32_t, 5> FakeSpirv{0x07230203U, 0x00010500U, 0, 1, 0};

constexpr std::array Attributes{
    VertexAttribute{.location = 0, .format = VertexFormat::Float3, .offset = 0},
    VertexAttribute{.location = 1, .format = VertexFormat::Float2, .offset = 12},
    VertexAttribute{.location = 2, .format = VertexFormat::Unorm8x4, .offset = 20},
};

GraphicsPipelineDesc validDesc() {
  return {.spirv = FakeSpirv,
          .vertex = {.stride = 24, .attributes = Attributes},
          .depth = {.test = true, .write = true},
          .colorFormat = Format::Bgra8Unorm,
          .depthFormat = Format::Depth32Float,
          .name = "test"};
}

} // namespace

TEST_CASE("a complete pipeline description is valid") {
  CHECK(validate(validDesc()));
}

TEST_CASE("a pipeline needs SPIR-V") {
  auto desc = validDesc();
  constexpr std::array<std::uint32_t, 5> NotSpirv{1, 2, 3, 4, 5};
  desc.spirv = NotSpirv;
  CHECK_FALSE(validate(desc));
  desc.spirv = {};
  CHECK_FALSE(validate(desc));
}

TEST_CASE("a pipeline needs a color format, and a depth format to use depth") {
  auto desc = validDesc();
  desc.colorFormat = Format::Undefined;
  CHECK_FALSE(validate(desc));
  desc.colorFormat = Format::Depth32Float;
  CHECK_FALSE(validate(desc));

  desc = validDesc();
  desc.depthFormat = Format::Undefined;
  CHECK_FALSE(validate(desc));
  desc.depth = {};
  CHECK(validate(desc));
  desc.depth.bias = true;
  CHECK_FALSE(validate(desc));

  desc = validDesc();
  desc.depthFormat = Format::Rgba8Unorm;
  CHECK_FALSE(validate(desc));
}

TEST_CASE("vertex attributes fit in the stride and use each location once") {
  auto desc = validDesc();
  desc.vertex.stride = 20;
  CHECK_FALSE(validate(desc));

  constexpr std::array Repeated{
      VertexAttribute{.location = 0, .format = VertexFormat::Float2, .offset = 0},
      VertexAttribute{.location = 0, .format = VertexFormat::Float2, .offset = 8},
  };
  desc = validDesc();
  desc.vertex = {.stride = 16, .attributes = Repeated};
  CHECK_FALSE(validate(desc));

  desc.vertex = {.stride = 0, .attributes = Attributes};
  CHECK_FALSE(validate(desc));
  // No vertex buffer at all: the shader builds its vertices.
  desc.vertex = {};
  CHECK(validate(desc));
}

TEST_CASE("image writes must lie inside the image and carry exactly their texels") {
  const Extent extent{.width = 8, .height = 4};
  CHECK(checkImageWrite(extent, Format::Rgba8Unorm, {.x = 0, .y = 0, .width = 8, .height = 4},
                        std::size_t{8} * 4 * 4));
  CHECK(checkImageWrite(extent, Format::Rgba8Unorm, {.x = 6, .y = 3, .width = 2, .height = 1},
                        std::size_t{2} * 4));
  CHECK_FALSE(checkImageWrite(extent, Format::Rgba8Unorm, {.x = 7, .y = 0, .width = 2, .height = 1},
                              std::size_t{2} * 4));
  CHECK_FALSE(
      checkImageWrite(extent, Format::Rgba8Unorm, {.x = -1, .y = 0, .width = 1, .height = 1}, 4));
  CHECK_FALSE(
      checkImageWrite(extent, Format::Rgba8Unorm, {.x = 0, .y = 0, .width = 0, .height = 1}, 0));
  CHECK_FALSE(
      checkImageWrite(extent, Format::Rgba8Unorm, {.x = 0, .y = 0, .width = 2, .height = 2}, 15));
  CHECK_FALSE(
      checkImageWrite(extent, Format::Depth32Float, {.x = 0, .y = 0, .width = 1, .height = 1}, 4));
}

TEST_CASE("flag enums combine") {
  constexpr ImageUsage Usage = ImageUsage::Sampled | ImageUsage::ColorTarget;
  CHECK(hasAll(Usage, ImageUsage::Sampled));
  CHECK(hasAll(Usage, ImageUsage::ColorTarget));
  CHECK_FALSE(hasAll(Usage, ImageUsage::DepthTarget));
  CHECK_FALSE(hasAll(ImageUsage::Sampled, Usage));
}

TEST_CASE("scissors are clipped to the target") {
  constexpr Extent Target{.width = 100, .height = 50};
  CHECK(clampToExtent({.x = 10, .y = 5, .width = 20, .height = 10}, Target) ==
        Rect{.x = 10, .y = 5, .width = 20, .height = 10});
  CHECK(clampToExtent({.x = -10, .y = -5, .width = 30, .height = 20}, Target) ==
        Rect{.x = 0, .y = 0, .width = 20, .height = 15});
  CHECK(clampToExtent({.x = 90, .y = 40, .width = 30, .height = 30}, Target) ==
        Rect{.x = 90, .y = 40, .width = 10, .height = 10});
  CHECK(clampToExtent({.x = 200, .y = -80, .width = 10, .height = 10}, Target).width == 0);
  CHECK(clampToExtent({.x = -80, .y = 10, .width = 10, .height = 10}, Target).width == 0);
}
