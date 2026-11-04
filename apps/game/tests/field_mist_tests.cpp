#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/map_images.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "field_mist.hpp"

using dwe::app::MistLayers;

namespace {

// The mist's layers after `frames` frames standing, the play time starting odd.
MistLayers standing(int frames) {
  MistLayers layers;
  for (int frame = 1; frame <= frames; ++frame) {
    layers.frame({0, 0}, frame % 2 == 0);
  }
  return layers;
}

float quadArea(const dwe::app::ScenePrim& prim) {
  const glm::vec2 size = prim.corners[3].offset - prim.corners[0].offset;
  return size.x * size.y;
}

} // namespace

TEST_CASE("the mist drifts a pixel every other frame, the back layer left and the front right") {
  // Measured on mist01: 100 frames standing move MIST_X_OFFSETS[0] -51 and MAIN_D_80134228[0] +51
  // (PLAYTIME_FRAMES 3460 to 3560), and the rows not at all; the pause menu does not stop it.
  const MistLayers layers = standing(100);
  CHECK(layers.backX()[0] == -0xA0 - 50);
  CHECK(layers.backX()[1] == -0xA0 - 50 + MistLayers::ScreenWidth);
  CHECK(layers.frontX()[0] == 0xA0 + 50);
  // 0xd2 + 0x140 reaches 0x1e0: one span back.
  CHECK(layers.frontX()[1] == 0xA0 + 50 + MistLayers::ScreenWidth - 0x280);
  CHECK(layers.rowsY()[0] == -0x78);
  CHECK(layers.rowsY()[1] == 0x78);
}

TEST_CASE("the back layer follows the camera, the front one 12/10 as fast, truncated each frame") {
  MistLayers layers;
  // An odd frame: no drift.
  layers.frame({0, 0}, false);
  // The camera moves 3 right and 47 down: the back layer and the rows move with the picture, the
  // front layer -3 * 12 / 10 = -3 (C truncates toward zero).
  layers.frame({3, 47}, false);
  CHECK(layers.backX()[0] == -0xA0 - 3);
  CHECK(layers.frontX()[0] == 0xA0 - 3);
  CHECK(layers.rowsY()[0] == -0x78 - 47);
  // 5 more: the front layer moves 6.
  layers.frame({8, 47}, false);
  CHECK(layers.backX()[0] == -0xA0 - 8);
  CHECK(layers.frontX()[0] == 0xA0 - 3 - 6);
  // Back to where it was in one frame: +8 and +9.
  layers.frame({0, 47}, false);
  CHECK(layers.backX()[0] == -0xA0);
  CHECK(layers.frontX()[0] == 0xA0 - 9 + 9);
  CHECK(layers.camera() == glm::ivec2{0, 47});
}

TEST_CASE("each edge goes one span back at its limit, as renderMist wraps it") {
  MistLayers layers;
  layers.frame({0, 0}, false);
  // The back layer's left edge passes 0xa0: 640 back.
  layers.frame({-0x150, 0}, false);
  CHECK(layers.backX()[0] == -0xA0 + 0x150 - 0x280);
  // The front one: 0xa0 + 0x150 * 12 / 10 = 0xa0 + 403 >= 0x1e0.
  CHECK(layers.frontX()[0] == 0xA0 + 403 - 0x280);
  // The rows' top edge under -0x167: 480 on.
  layers.frame({-0x150, 0x100}, false);
  CHECK(layers.rowsY()[0] == -0x78 - 0x100 + 0x1E0);
  // A frame on another map keeps only the camera.
  layers.keepCamera({500, 500});
  CHECK(layers.camera() == glm::ivec2{500, 500});
  CHECK(layers.rowsY()[0] == -0x78 - 0x100 + 0x1E0);
}

TEST_CASE("the layers cover the original's screen with quads cut to it, the front one mirrored") {
  const MistLayers layers = standing(10);
  const dwe::app::MistTextures textures{.back = dwe::rhi::TextureId{1},
                                        .front = dwe::rhi::TextureId{2}};
  const glm::vec2 areaMin{-160.0F, -120.0F};
  const glm::vec2 areaMax{160.0F, 120.0F};
  const auto prims = dwe::app::mistPrims(layers, textures, glm::vec2{0.0F}, areaMin, areaMax);
  float front = 0.0F;
  float back = 0.0F;
  bool frontFirst = true;
  bool seenBack = false;
  for (const dwe::app::ScenePrim& prim : prims) {
    CHECK(prim.blend == dwe::app::ScenePrimBlend::Additive);
    CHECK(prim.stpMask);
    for (const auto& corner : prim.corners) {
      CHECK(corner.offset.x >= areaMin.x);
      CHECK(corner.offset.x <= areaMax.x);
      CHECK(corner.offset.y >= areaMin.y);
      CHECK(corner.offset.y <= areaMax.y);
    }
    if (prim.texture.index == 2) {
      frontFirst = frontFirst && !seenBack;
      front += quadArea(prim);
      // 0x50 at full strength; mirrored: the left corners read the larger u.
      CHECK(prim.color.r == doctest::Approx(0.625F));
      CHECK(prim.color.a == doctest::Approx(1.0F));
      CHECK(prim.corners[0].uv.x > prim.corners[1].uv.x);
    } else {
      seenBack = true;
      back += quadArea(prim);
      // B + F / 4 at 0x96.
      CHECK(prim.color.r == doctest::Approx(0x96 / 128.0F));
      CHECK(prim.color.a == doctest::Approx(0.25F));
      CHECK(prim.corners[0].uv.x < prim.corners[1].uv.x);
    }
  }
  CHECK(frontFirst);
  CHECK(front == doctest::Approx(320.0F * 240.0F));
  CHECK(back == doctest::Approx(320.0F * 240.0F));
  // Off the phase two quads a side; the rows sit on the screen's edges: one row.
  CHECK(prims.size() == 4);
}

TEST_CASE("a wider window shows the mist repeated to its edges, and no layer without texture") {
  const MistLayers layers = standing(3);
  const glm::vec2 areaMin{-300.0F, -120.0F};
  const glm::vec2 areaMax{300.0F, 120.0F};
  const auto prims =
      dwe::app::mistPrims(layers, {.back = dwe::rhi::TextureId{1}, .front = std::nullopt},
                          glm::vec2{7.5F, -2.0F}, areaMin, areaMax);
  float area = 0.0F;
  for (const dwe::app::ScenePrim& prim : prims) {
    CHECK(prim.texture.index == 1);
    area += quadArea(prim);
  }
  CHECK(area == doctest::Approx(600.0F * 240.0F));
  // A quad's texels match its place: the back layer's left edge, shifted, reads u 0.
  const float left = static_cast<float>(layers.backX()[0]) + 7.5F;
  for (const dwe::app::ScenePrim& prim : prims) {
    const float u = prim.corners[0].uv.x * 256.0F / 255.0F;
    const float x = prim.corners[0].offset.x;
    const float into = std::fmod(std::fmod(x - left, 320.0F) + 320.0F, 320.0F);
    CHECK(u == doctest::Approx(into / 320.0F).epsilon(1e-4));
  }
  CHECK(dwe::app::mistPrims(layers, {}, glm::vec2{0.0F}, areaMin, areaMax).empty());
}

TEST_CASE("the mist's picture is the 4-bit page at 0x2c0 with a palette of the first one's CLUT") {
  dwe::assets::MapImages images;
  dwe::assets::MapTexturePage page;
  page.bitsPerPixel = 4;
  page.vramX = 0x2C0;
  page.clutRow = 486;
  page.image.width = 256;
  page.image.height = 256;
  page.image.indices.assign(std::size_t{256} * 256, 0);
  page.image.indices[0] = 1;
  page.image.indices[(199 * 256) + 255] = 2;
  // Palette 0: colour 1 with STP (0xd294), colour 2 without; palette 1 all transparent.
  std::vector<dwe::assets::PaletteColor> first(16);
  first[1] = dwe::assets::colorFromOriginal(0xD294);
  first[2] = dwe::assets::colorFromOriginal(0x7FFF);
  page.cluts = {first, std::vector<dwe::assets::PaletteColor>(16)};
  images.pages.push_back(page);

  const auto picture = dwe::app::mistImage(images, 0);
  REQUIRE(picture.has_value());
  CHECK(picture->width == 256);
  CHECK(picture->height == 200);
  CHECK(picture->pixels[3] == std::byte{dwe::assets::SemiTransparentAlpha});
  CHECK(picture->pixels[7] == std::byte{0});
  const std::size_t last = ((std::size_t{199} * 256) + 255) * 4;
  CHECK(picture->pixels[last + 3] == std::byte{0xFF});
  CHECK(dwe::app::mistImage(images, 1).has_value());
  CHECK_FALSE(dwe::app::mistImage(images, 2).has_value());
  // No page at 0x2c0: no mist.
  images.pages.front().vramX = 0x280;
  CHECK_FALSE(dwe::app::mistImage(images, 0).has_value());
}
