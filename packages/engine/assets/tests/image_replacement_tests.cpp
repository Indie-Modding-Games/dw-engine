#include <dwe/assets/file_system.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/image_replacement.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <doctest/doctest.h>
#include <lodepng.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using dwe::assets::Image;
using dwe::assets::MapImages;
using dwe::assets::MemoryFileSystem;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

// An 8-bit palette PNG whose pixels are all `index`; palette entry e is red e, opaque. Index 0 is
// opaque black, which the map loader reads as semi-transparent.
std::vector<std::byte> palettePng(unsigned width, unsigned height, unsigned char index) {
  lodepng::State state;
  for (unsigned entry = 0; entry < 8; ++entry) {
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

// An RGBA PNG of one colour, alpha 255, as an image editor saves a redrawn or upscaled image.
std::vector<std::byte> rgbaPng(std::uint32_t width, std::uint32_t height, std::uint8_t green) {
  Image image{.width = width, .height = height, .pixels = {}};
  for (std::size_t pixel = 0; pixel < std::size_t{width} * height; ++pixel) {
    for (const std::uint8_t channel :
         {std::uint8_t{10}, green, std::uint8_t{30}, std::uint8_t{255}}) {
      image.pixels.push_back(std::byte{channel});
    }
  }
  auto png = dwe::assets::encodePng(image);
  REQUIRE(png.has_value());
  return *png;
}

constexpr std::string_view PageMeta =
    "image_rect:\n  x: 384\n  y: 0\nclut_rect:\n  x: 0\n  y: 480\npalettes: []\n";

// The base files of map "m" (2 x 1 tiles): day and night backgrounds of index 3, the left tile's
// first texel index 0 (semi-transparent), and one texture page.
void addBase(MemoryFileSystem& memory) {
  memory.addFile("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("base/map/m/background_day.png", palettePng(256, 128, 3));
  memory.addFile("base/map/m/background_night.png", palettePng(256, 128, 3));
  memory.addFile("base/map/m/texture8/000.png", palettePng(256, 256, 1));
  memory.addFile("base/map/m/texture8/000_meta.yaml", PageMeta);
}

dwe::core::Result<MapImages> loadWithPlayerPack(MemoryFileSystem& memory) {
  memory.addFile("mine/pack.yaml", "id: custom_images\nversion: 1.0.0\nlayer: user\n");
  const std::vector<std::filesystem::path> roots{"base", "mine"};
  auto vfs = dwe::assets::VirtualFileSystem::mount(memory, roots);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  dwe::assets::MapDocument document;
  document.id = "m";
  document.tiles = {.width = 2, .height = 1, .cells = {0, 1}};
  document.assets = dwe::assets::findMapAssets(*vfs, "m");
  return dwe::assets::loadMapImages(*vfs, document);
}

std::uint8_t channelAt(const Image& image, std::uint32_t x, std::uint32_t y, std::size_t channel) {
  return std::to_integer<std::uint8_t>(
      image.pixels.at((((std::size_t{y} * image.width) + x) * 4) + channel));
}

} // namespace

TEST_CASE("a replacement is a whole number of times larger, the same on both axes") {
  using dwe::assets::replacementScale;
  CHECK(replacementScale(256, 128, 256, 128) == 1U);
  CHECK(replacementScale(256, 128, 512, 256) == 2U);
  CHECK(replacementScale(256, 128, 1024, 512) == 4U);
  CHECK(replacementScale(256, 128, 2048, 1024) == 8U);
  CHECK_FALSE(replacementScale(256, 128, 4096, 2048).has_value());
  CHECK_FALSE(replacementScale(256, 128, 512, 300).has_value());
  CHECK_FALSE(replacementScale(256, 128, 512, 128).has_value());
  CHECK_FALSE(replacementScale(256, 128, 384, 192).has_value());
  CHECK_FALSE(replacementScale(256, 128, 128, 64).has_value());
  CHECK_FALSE(replacementScale(0, 128, 0, 128).has_value());
}

TEST_CASE("the replacement of one palette of an image is named after it") {
  const auto image = dwe::assets::AssetId::parse("texture/etcdat/fi_info");
  REQUIRE(image.has_value());
  const auto id = dwe::assets::paletteImageId(*image, 12);
  REQUIRE(id.has_value());
  CHECK(id->str() == "texture/etcdat/fi_info_palette12");
}

TEST_CASE("a player's background twice as large replaces it and keeps the game's data") {
  MemoryFileSystem memory;
  addBase(memory);
  memory.addFile("mine/map/m/background_day.png", rgbaPng(512, 256, 200));
  const auto images = loadWithPlayerPack(memory);
  REQUIRE_MESSAGE(images.has_value(), errorOf(images));
  // The game's indices and palettes are still the base's.
  CHECK(images->background.width == 256);
  CHECK(images->backgroundPalettes.size() == 2);
  REQUIRE(images->replacement.has_value());
  CHECK(images->replacement->scale == 2);
  REQUIRE(images->replacement->variants.size() == 2);
  const Image& day = images->replacement->variants[0];
  CHECK(day.width == 512);
  CHECK(day.height == 256);
  CHECK(channelAt(day, 100, 100, 1) == 200);
  CHECK(channelAt(day, 100, 100, 3) == 255);
  // The night has no image of its own: the base's, enlarged, red 3 from its palette.
  const Image& night = images->replacement->variants[1];
  CHECK(night.width == 512);
  CHECK(channelAt(night, 511, 255, 0) == 3);
}

TEST_CASE("a replacement takes the class of the background texel under each pixel") {
  MemoryFileSystem memory;
  addBase(memory);
  // Index 0 is opaque black in the base's palette: semi-transparent once loaded.
  memory.addFile("base/map/m/background_day.png", palettePng(256, 128, 0));
  memory.addFile("base/map/m/background_night.png", palettePng(256, 128, 0));
  memory.addFile("mine/map/m/background_day.png", rgbaPng(512, 256, 200));
  const auto images = loadWithPlayerPack(memory);
  REQUIRE_MESSAGE(images.has_value(), errorOf(images));
  REQUIRE(images->replacement.has_value());
  CHECK(channelAt(images->replacement->variants[0], 7, 9, 3) == dwe::assets::SemiTransparentAlpha);
}

TEST_CASE("a player's background that does not fit is left out and the game's is drawn") {
  MemoryFileSystem memory;
  addBase(memory);
  SUBCASE("another aspect ratio") {
    memory.addFile("mine/map/m/background_day.png", rgbaPng(512, 200, 200));
  }
  SUBCASE("not a whole number of times larger") {
    memory.addFile("mine/map/m/background_day.png", rgbaPng(384, 192, 200));
  }
  SUBCASE("not a PNG") {
    memory.addFile("mine/map/m/background_day.png", "not an image");
  }
  const auto images = loadWithPlayerPack(memory);
  REQUIRE_MESSAGE(images.has_value(), errorOf(images));
  CHECK_FALSE(images->replacement.has_value());
  CHECK(images->background.width == 256);
}

TEST_CASE("the variants of a replacement share one scale") {
  MemoryFileSystem memory;
  addBase(memory);
  memory.addFile("mine/map/m/background_day.png", rgbaPng(512, 256, 200));
  memory.addFile("mine/map/m/background_night.png", rgbaPng(1024, 512, 100));
  const auto images = loadWithPlayerPack(memory);
  REQUIRE_MESSAGE(images.has_value(), errorOf(images));
  REQUIRE(images->replacement.has_value());
  CHECK(images->replacement->scale == 2);
  // The night's 4x image is left out: the base's night, enlarged twice.
  CHECK(channelAt(images->replacement->variants[1], 0, 0, 1) == 0);
}

TEST_CASE("a player's image of a texture page never breaks the map") {
  MemoryFileSystem memory;
  addBase(memory);
  memory.addFile("mine/map/m/texture8/000.png", rgbaPng(512, 512, 200));
  const auto images = loadWithPlayerPack(memory);
  REQUIRE_MESSAGE(images.has_value(), errorOf(images));
  CHECK_FALSE(images->replacement.has_value());
  CHECK(images->pages.size() == 1);
}
