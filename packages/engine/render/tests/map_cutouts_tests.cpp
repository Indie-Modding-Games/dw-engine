// HD scenery's cut-outs (render::MapCutouts): which depth-layer sprites take their colours from the
// background under them (docs/architecture/remaster-pipeline.md, «Alta definición en el motor»).

#include <dwe/assets/map_images.hpp>
#include <dwe/render/map_cutouts.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace dwe;

namespace {

constexpr std::uint32_t Side = 8;
// Index 0 is 0x0000 (never drawn) in every variant; the others are colours.
constexpr std::uint8_t Clear = 0;

// A background whose index at (x, y) is 1 + x + y * Side (never Clear), a day and a night
// palette, and a sheet of one texture: sprite 0 the rectangle at (1, 1) of 3 x 2 texels, cut from
// the background at (4, 2) with its top-left texel clear; sprite 1 the same texels with a colour of
// their own (a fixed palette).
assets::MapImages cutImages() {
  assets::MapImages images;
  images.background = {.width = Side, .height = Side, .indices = {}, .palette = {}};
  for (std::uint32_t y = 0; y < Side; ++y) {
    for (std::uint32_t x = 0; x < Side; ++x) {
      images.background.indices.push_back(static_cast<std::uint8_t>(1 + x + (y * Side)));
    }
  }
  for (int variant = 0; variant < 2; ++variant) {
    std::vector<assets::PaletteColor> palette(256, {.red = 9, .green = 9, .blue = 9, .alpha = 255});
    palette[Clear] = {};
    images.backgroundPalettes.push_back(palette);
  }
  assets::MapSpriteSheet::Texture page{.timeOfDayPalette = true,
                                       .indices = {.width = 6,
                                                   .height = 4,
                                                   .indices = std::vector<std::uint8_t>(24, Clear),
                                                   .palette = {}},
                                       .colors = {}};
  for (std::uint32_t row = 0; row < 2; ++row) {
    for (std::uint32_t column = 0; column < 3; ++column) {
      const std::size_t under = 4 + column + ((2 + row) * Side);
      page.indices.indices[1 + column + ((1 + row) * 6)] = images.background.indices[under];
    }
  }
  page.indices.indices[1 + 6] = Clear;
  images.sprites.textures.push_back(page);
  assets::MapSpriteSheet::Texture colored{.timeOfDayPalette = false, .indices = {}, .colors = {}};
  colored.colors.width = 6;
  colored.colors.height = 4;
  colored.colors.pixels.resize(6 * 4 * 4);
  images.sprites.textures.push_back(colored);
  images.sprites.placements = {{.texture = 0, .texel = {1, 1}, .size = {3, 2}},
                               {.texture = 1, .texel = {1, 1}, .size = {3, 2}}};
  return images;
}

} // namespace

TEST_CASE("a sprite cut out of the background is one only where it was cut") {
  render::MapCutouts cutouts{cutImages()};
  CHECK(cutouts.isCutout(0, {4.0F, 2.0F}));
  // Asked again, from what it remembers.
  CHECK(cutouts.isCutout(0, {4.0F, 2.0F}));
  CHECK_FALSE(cutouts.isCutout(0, {3.0F, 2.0F}));
  CHECK_FALSE(cutouts.isCutout(0, {4.0F, 3.0F}));
  // Between pixels it cannot match the background texel for texel.
  CHECK_FALSE(cutouts.isCutout(0, {4.5F, 2.0F}));
  // Out of the background, or of the sprites.
  CHECK_FALSE(cutouts.isCutout(0, {6.0F, 2.0F}));
  CHECK_FALSE(cutouts.isCutout(0, {-1.0F, 2.0F}));
  CHECK_FALSE(cutouts.isCutout(7, {4.0F, 2.0F}));
}

TEST_CASE("only the texels a sprite draws must match the background, in some variant") {
  assets::MapImages images = cutImages();
  // Under the clear texel the background may hold anything.
  images.background.indices[4 + (2 * Side)] = 200;
  CHECK(render::MapCutouts{images}.isCutout(0, {4.0F, 2.0F}));
  // Drawn at night only, it must match.
  images.backgroundPalettes[1][Clear] = {.red = 1, .green = 2, .blue = 3, .alpha = 255};
  CHECK_FALSE(render::MapCutouts{images}.isCutout(0, {4.0F, 2.0F}));
}

TEST_CASE("a sprite with a palette of its own, or that draws nothing, is no cut-out") {
  assets::MapImages images = cutImages();
  CHECK_FALSE(render::MapCutouts{images}.isCutout(1, {4.0F, 2.0F}));
  std::ranges::fill(images.sprites.textures[0].indices.indices, Clear);
  CHECK_FALSE(render::MapCutouts{images}.isCutout(0, {4.0F, 2.0F}));
}

TEST_CASE("a sprite that differs from the background in one drawn texel is no cut-out") {
  assets::MapImages images = cutImages();
  images.sprites.textures[0].indices.indices[3 + (2 * 6)] = 77;
  CHECK_FALSE(render::MapCutouts{images}.isCutout(0, {4.0F, 2.0F}));
}
