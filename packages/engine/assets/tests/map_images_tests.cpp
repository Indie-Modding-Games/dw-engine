#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <doctest/doctest.h>
#include <lodepng.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using dwe::assets::MapImages;
using dwe::assets::MapOrder;
using dwe::assets::MapSprite;
using dwe::assets::MapSpriteSheet;
using dwe::assets::MapTexturePage;
using dwe::assets::PaletteColor;
using dwe::assets::SpriteBlend;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

std::vector<PaletteColor> solidPalette(std::uint8_t red, std::size_t size) {
  return std::vector<PaletteColor>(size,
                                   PaletteColor{.red = red, .green = 0, .blue = 0, .alpha = 255});
}

// A 256 x 256 page whose every index is 1.
MapTexturePage page(std::int32_t bitsPerPixel, std::int32_t vramX, std::int32_t clutRow,
                    std::vector<std::vector<PaletteColor>> cluts) {
  return {
      .bitsPerPixel = bitsPerPixel,
      .vramX = vramX,
      .clutRow = clutRow,
      .image = {.width = 256,
                .height = 256,
                .indices = std::vector<std::uint8_t>(std::size_t{256} * 256, 1),
                .palette = {}},
      .cluts = std::move(cluts),
  };
}

MapSprite sprite(std::int32_t texX, std::int32_t clut, glm::ivec3 position = {0, 0, 0}) {
  return {.texX = texX,
          .texY = 8,
          .width = 16,
          .height = 24,
          .position = position,
          .clut = clut,
          .transparency = 4};
}

// The red of a fixed-palette sprite's first texel.
std::uint8_t redOf(const MapSpriteSheet& sheet, std::size_t spriteIndex) {
  const auto& placement = sheet.placements.at(spriteIndex);
  REQUIRE(placement.texture != MapSpriteSheet::NoTexture);
  const MapSpriteSheet::Texture& texture = sheet.textures.at(placement.texture);
  REQUIRE_FALSE(texture.timeOfDayPalette);
  return std::to_integer<std::uint8_t>(texture.colors.pixels.at(0));
}

// An 8-bit palette PNG of one index, with a palette of `colors` entries whose red is the entry.
std::vector<std::byte> palettePng(unsigned width, unsigned height, unsigned char index,
                                  unsigned colors) {
  lodepng::State state;
  for (unsigned entry = 0; entry < colors; ++entry) {
    const auto value = static_cast<unsigned char>(entry);
    REQUIRE(lodepng_palette_add(&state.info_png.color, value, 0, 0, 255) == 0);
    REQUIRE(lodepng_palette_add(&state.info_raw, value, 0, 0, 255) == 0);
  }
  state.info_png.color.colortype = LCT_PALETTE;
  state.info_png.color.bitdepth = 8;
  state.info_raw.colortype = LCT_PALETTE;
  state.info_raw.bitdepth = 8;
  state.encoder.auto_convert = 0;
  std::vector<unsigned char> png;
  REQUIRE(lodepng::encode(png, std::vector<unsigned char>(std::size_t{width} * height, index),
                          width, height, state) == 0);
  std::vector<std::byte> bytes(png.size());
  for (std::size_t at = 0; at < png.size(); ++at) {
    bytes[at] = std::byte{png[at]};
  }
  return bytes;
}

constexpr std::string_view PageMeta =
    "image_rect:\n  x: 384\n  y: 0\nclut_rect:\n  x: 0\n  y: 480\npalettes: []\n";

constexpr std::string_view ShortFourBitMeta =
    "image_rect:\n  x: 704\n  y: 216\nclut_rect:\n  x: 0\n  y: 486\npalettes: []\n";

std::uint8_t indexAt(const MapTexturePage& page, std::uint32_t x, std::uint32_t y) {
  return page.image.indices.at((static_cast<std::size_t>(y) * page.image.width) + x);
}

// Loads the images of map "m" (2 x 1 tiles, one sprite with clut -1) from the given files.
dwe::core::Result<MapImages> loadFixture(dwe::assets::MemoryFileSystem& memory) {
  memory.addFile("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  const std::vector<std::filesystem::path> roots{"base"};
  auto vfs = dwe::assets::VirtualFileSystem::mount(memory, roots);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  dwe::assets::MapDocument document;
  document.id = "m";
  document.tiles = {.width = 2, .height = 1, .cells = {0, 1}};
  document.objects = dwe::assets::MapObjects{.sprites = {sprite(0, -1)}, .instances = {}};
  document.assets = dwe::assets::findMapAssets(*vfs, "m");
  return dwe::assets::loadMapImages(*vfs, document);
}

} // namespace

TEST_CASE("15-bit colours expand their bits, 0x0000 is transparent and STP is kept in the alpha") {
  CHECK(dwe::assets::colorFromOriginal(0x0000) == PaletteColor{});
  CHECK(dwe::assets::colorFromOriginal(0x7FFF) ==
        PaletteColor{.red = 255, .green = 255, .blue = 255, .alpha = 255});
  CHECK(dwe::assets::colorFromOriginal(0x8000) ==
        PaletteColor{.red = 0, .green = 0, .blue = 0, .alpha = dwe::assets::SemiTransparentAlpha});
  CHECK(
      dwe::assets::colorFromOriginal(0x801F) ==
      PaletteColor{.red = 255, .green = 0, .blue = 0, .alpha = dwe::assets::SemiTransparentAlpha});
  const std::vector<PaletteColor> fromPng = dwe::assets::paletteFromPng({
      PaletteColor{},
      PaletteColor{.red = 0, .green = 0, .blue = 0, .alpha = 255},
      PaletteColor{.red = 8, .green = 0, .blue = 0, .alpha = 255},
  });
  REQUIRE(fromPng.size() == 3);
  CHECK(fromPng[0] == PaletteColor{});
  CHECK(fromPng[1] == dwe::assets::colorFromOriginal(0x8000));
  CHECK(fromPng[2] == PaletteColor{.red = 8, .green = 0, .blue = 0, .alpha = 255});
  CHECK(dwe::assets::colorFromOriginal(0x0010) ==
        PaletteColor{.red = 132, .green = 0, .blue = 0, .alpha = 255});
}

TEST_CASE("a texture page reads its VRAM place and CLUT rows from its metadata") {
  const std::string meta =
      "source: \"MAP/X.MAP\"\nentry: 2\nmode: indexed4\nwidth: 256\nheight: 256\n"
      "image_rect:\n  x: 640\n  y: 216\n  width: 64\n  height: 32\n"
      "clut_rect:\n  x: 0\n  y: 486\n  width: 16\n  height: 2\n"
      "png: indexed\npng_palette: 0\npalettes:\n- \"0000 7fff\"\n- \"001f 8000\"\n";
  const auto parsed = dwe::assets::parseMapTexturePage(4, {}, meta);
  REQUIRE_MESSAGE(parsed.has_value(), errorOf(parsed));
  CHECK(parsed->vramX == 640);
  CHECK(parsed->vramY == 216);
  CHECK(parsed->clutRow == 486);
  REQUIRE(parsed->cluts.size() == 2);
  CHECK(parsed->cluts[0][0] == PaletteColor{});
  CHECK(parsed->cluts[1][0] == PaletteColor{.red = 255, .green = 0, .blue = 0, .alpha = 255});
  CHECK_FALSE(dwe::assets::parseMapTexturePage(4, {}, "image_rect: {x: 1}\n").has_value());
}

TEST_CASE("sprites take the page and palette their texture X and CLUT reference point at") {
  const std::vector<MapTexturePage> pages{
      page(8, 384, 480, {solidPalette(1, 256)}),
      page(8, 512, 484, {solidPalette(2, 256)}),
      page(4, 640, 486, {solidPalette(3, 16), solidPalette(4, 16)}),
      page(4, 704, 486, {solidPalette(5, 16)}),
  };
  const std::vector<std::vector<PaletteColor>> background{
      solidPalette(10, 256), solidPalette(11, 256), solidPalette(12, 256), solidPalette(13, 256)};
  const std::vector<MapSprite> sprites{
      sprite(0, -1),   // first 8-bit page, time-of-day palette
      sprite(256, -1), // second 8-bit page, time-of-day palette
      sprite(1024, 1), // 4-bit page at 384 + 4 * 64 = 640, row 1 of the first 4-bit page's CLUT
      sprite(1280, 0), // 4-bit page at 704: still the first 4-bit page's CLUT
      sprite(256, 16), // row 484: the second 8-bit page's own CLUT
      sprite(0, 17),   // row 485: background palette 4, which does not exist
      sprite(0, 15),   // the first 4-bit page has no row 15
      sprite(512, -1), // no page at 640 for 8 bits
      sprite(0, -1),   // the first sprite's texture again
  };
  const MapSpriteSheet sheet = dwe::assets::buildMapSpriteSheet(sprites, pages, background);
  REQUIRE(sheet.placements.size() == sprites.size());

  // Time-of-day sprites keep their page's indices, one texture per page, for every time of day.
  for (const std::size_t index : {0U, 1U}) {
    const auto& placement = sheet.placements[index];
    REQUIRE(placement.texture != MapSpriteSheet::NoTexture);
    const MapSpriteSheet::Texture& texture = sheet.textures[placement.texture];
    CHECK(texture.timeOfDayPalette);
    CHECK(texture.indices.indices.at(0) == 1);
    CHECK(texture.indices.palette.empty());
    CHECK(texture.width() == 256);
  }
  CHECK(sheet.placements[0].texture != sheet.placements[1].texture);
  CHECK(sheet.placements[8].texture == sheet.placements[0].texture);

  CHECK(redOf(sheet, 2) == 4);
  CHECK(redOf(sheet, 3) == 3);
  CHECK(redOf(sheet, 4) == 2);
  CHECK(sheet.placements[5].texture == MapSpriteSheet::NoTexture);
  CHECK(sheet.placements[6].texture == MapSpriteSheet::NoTexture);
  CHECK(sheet.placements[7].texture == MapSpriteSheet::NoTexture);
  CHECK(sheet.placements[1].texel == glm::ivec2{0, 8});
  CHECK(sheet.placements[1].size == glm::ivec2{16, 24});
  // One texture per page and palette.
  CHECK(sheet.textures.size() == 5);

  // clut 16 + 468 = 484 falls back to a background palette (481 + 3) when no 8-bit page uploads a
  // CLUT there.
  const std::vector<MapTexturePage> noRow484{pages[0], pages[2]};
  const MapSpriteSheet fallback =
      dwe::assets::buildMapSpriteSheet(std::vector{sprite(0, 16)}, noRow484, background);
  CHECK(redOf(fallback, 0) == 13);

  // Without background palettes, time-of-day sprites have nothing to read.
  const MapSpriteSheet none =
      dwe::assets::buildMapSpriteSheet(std::vector{sprite(0, -1)}, pages, {});
  CHECK(none.placements.at(0).texture == MapSpriteSheet::NoTexture);
}

TEST_CASE("semi-transparency modes map to blends") {
  CHECK(dwe::assets::spriteBlend(0) == SpriteBlend::Average);
  CHECK(dwe::assets::spriteBlend(1) == SpriteBlend::Add);
  CHECK(dwe::assets::spriteBlend(2) == SpriteBlend::Subtract);
  CHECK(dwe::assets::spriteBlend(3) == SpriteBlend::AddQuarter);
  CHECK(dwe::assets::spriteBlend(4) == SpriteBlend::Opaque);
}

TEST_CASE("a sprite's order value is its view depth in ordering-table entries, clamped") {
  const MapSprite plain = sprite(0, -1);
  CHECK(dwe::assets::mapSpriteOrderValue(plain, 0x100 * 16.0F) == 0x100);
  CHECK(dwe::assets::mapSpriteOrderValue(plain, (0x100 * 16.0F) + 15.9F) == 0x100);
  CHECK(dwe::assets::mapSpriteOrderValue(plain, 10.0F) == MapOrder::Nearest);
  CHECK(dwe::assets::mapSpriteOrderValue(plain, 0x1000 * 16.0F) == MapOrder::Farthest);
  CHECK(dwe::assets::mapSpriteOrderValue(plain, (0xFF7 * 16.0F)) == 0xFF7);
  CHECK(dwe::assets::mapSpriteOrderValue(sprite(0, -1, {0, MapOrder::BehindEverythingY, 0}),
                                         100.0F) == MapOrder::BehindEverything);
}

TEST_CASE("map images load when the backgrounds have the document's size and fail otherwise") {
  SUBCASE("matching sizes") {
    dwe::assets::MemoryFileSystem memory;
    memory.addFile("base/map/m/background_day.png", palettePng(256, 128, 3, 8));
    memory.addFile("base/map/m/background_night.png", palettePng(256, 128, 3, 8));
    memory.addFile("base/map/m/texture8/000.png", palettePng(256, 256, 1, 4));
    memory.addFile("base/map/m/texture8/000_meta.yaml", PageMeta);
    const auto images = loadFixture(memory);
    REQUIRE_MESSAGE(images.has_value(), errorOf(images));
    CHECK(images->background.width == 256);
    CHECK(images->backgroundVariants == std::vector<std::string>{"day", "night"});
    CHECK(images->backgroundPalettes.size() == 2);
    REQUIRE(images->sprites.placements.size() == 1);
    CHECK(images->sprites.placements[0].texture == 0);
  }
  SUBCASE("a background that is not tiles x 128") {
    dwe::assets::MemoryFileSystem memory;
    memory.addFile("base/map/m/background_day.png", palettePng(256, 120, 3, 8));
    const auto images = loadFixture(memory);
    REQUIRE_FALSE(images.has_value());
    CHECK(images.error().message.find("2x1 tiles") != std::string::npos);
  }
  SUBCASE("variants of another size") {
    dwe::assets::MemoryFileSystem memory;
    memory.addFile("base/map/m/background_day.png", palettePng(256, 128, 3, 8));
    memory.addFile("base/map/m/background_night.png", palettePng(128, 128, 3, 8));
    CHECK_FALSE(loadFixture(memory).has_value());
  }
  SUBCASE("variants with other pixels") {
    dwe::assets::MemoryFileSystem memory;
    memory.addFile("base/map/m/background_day.png", palettePng(256, 128, 3, 8));
    memory.addFile("base/map/m/background_night.png", palettePng(256, 128, 4, 8));
    CHECK_FALSE(loadFixture(memory).has_value());
  }
  SUBCASE("an entry three pages wide is cut into its pages") {
    dwe::assets::MemoryFileSystem memory;
    memory.addFile("base/map/m/background_day.png", palettePng(256, 128, 3, 8));
    memory.addFile("base/map/m/texture8/000.png", palettePng(768, 256, 1, 4));
    memory.addFile("base/map/m/texture8/000_meta.yaml", PageMeta);
    const auto images = loadFixture(memory);
    REQUIRE_MESSAGE(images.has_value(), errorOf(images));
    REQUIRE(images->pages.size() == 3);
    for (std::size_t index = 0; index < images->pages.size(); ++index) {
      const MapTexturePage& page = images->pages[index];
      CHECK(page.vramX == 384 + (static_cast<std::int32_t>(index) * 128));
      CHECK(page.image.width == 256);
      CHECK(page.image.indices.size() == std::size_t{256} * 256);
    }
  }
  SUBCASE("an entry shorter than a page lands at its rows of the page") {
    // gomi02's 4-bit entry: 256 x 32 texels at VRAM (704, 216).
    dwe::assets::MemoryFileSystem memory;
    memory.addFile("base/map/m/background_day.png", palettePng(256, 128, 3, 8));
    memory.addFile("base/map/m/texture4/000.png", palettePng(256, 32, 5, 16));
    memory.addFile("base/map/m/texture4/000_meta.yaml", ShortFourBitMeta);
    const auto images = loadFixture(memory);
    REQUIRE_MESSAGE(images.has_value(), errorOf(images));
    REQUIRE(images->pages.size() == 1);
    const MapTexturePage& page = images->pages[0];
    CHECK(page.bitsPerPixel == 4);
    CHECK(page.vramX == 704);
    REQUIRE(page.image.indices.size() == std::size_t{256} * 256);
    CHECK(indexAt(page, 0, 215) == 0);
    CHECK(indexAt(page, 0, 216) == 5);
    CHECK(indexAt(page, 255, 247) == 5);
    CHECK(indexAt(page, 255, 248) == 0);
  }
  SUBCASE("a later entry overwrites what it covers of an earlier one, whatever its depth") {
    // The 8-bit page at 640 spans x 640-767: the 4-bit entry at 704 is its right half's rows
    // 216-247, two 4-bit 5s to a byte.
    dwe::assets::MemoryFileSystem memory;
    memory.addFile("base/map/m/background_day.png", palettePng(256, 128, 3, 8));
    memory.addFile("base/map/m/texture8/000.png", palettePng(256, 256, 1, 4));
    memory.addFile("base/map/m/texture8/000_meta.yaml",
                   "image_rect:\n  x: 640\n  y: 0\nclut_rect:\n  x: 0\n  y: 484\npalettes: []\n");
    memory.addFile("base/map/m/texture4/000.png", palettePng(256, 32, 5, 16));
    memory.addFile("base/map/m/texture4/000_meta.yaml", ShortFourBitMeta);
    const auto images = loadFixture(memory);
    REQUIRE_MESSAGE(images.has_value(), errorOf(images));
    REQUIRE(images->pages.size() == 2);
    const MapTexturePage& eightBit = images->pages[0];
    CHECK(eightBit.vramX == 640);
    CHECK(indexAt(eightBit, 127, 216) == 1);
    CHECK(indexAt(eightBit, 128, 216) == 0x55);
    CHECK(indexAt(eightBit, 255, 247) == 0x55);
    CHECK(indexAt(eightBit, 128, 248) == 1);
    CHECK(indexAt(images->pages[1], 0, 216) == 5);
  }
  SUBCASE("an entry narrower than a page is the left part of the page its X is in") {
    dwe::assets::MemoryFileSystem memory;
    memory.addFile("base/map/m/background_day.png", palettePng(256, 128, 3, 8));
    memory.addFile("base/map/m/texture8/000.png", palettePng(128, 256, 1, 4));
    memory.addFile("base/map/m/texture8/000_meta.yaml", PageMeta);
    const auto images = loadFixture(memory);
    REQUIRE_MESSAGE(images.has_value(), errorOf(images));
    REQUIRE(images->pages.size() == 1);
    CHECK(indexAt(images->pages[0], 127, 0) == 1);
    CHECK(indexAt(images->pages[0], 128, 0) == 0);
  }
  SUBCASE("an entry that does not fit the map's VRAM") {
    dwe::assets::MemoryFileSystem memory;
    memory.addFile("base/map/m/background_day.png", palettePng(256, 128, 3, 8));
    memory.addFile("base/map/m/texture8/000.png", palettePng(256, 256, 1, 4));
    memory.addFile("base/map/m/texture8/000_meta.yaml",
                   "image_rect:\n  x: 960\n  y: 0\nclut_rect:\n  x: 0\n  y: 480\npalettes: []\n");
    const auto images = loadFixture(memory);
    REQUIRE_FALSE(images.has_value());
    CHECK(images.error().message.find("do not fit") != std::string::npos);
  }
}
