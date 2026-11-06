// The player's images in the UI (assets/image_replacement.hpp): `<id>_palette<N>` drawn instead of
// palette N of an indexed image, at the original's size.

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/ui/image_reference.hpp>

#include <RmlUi/Core/Types.h>
#include <doctest/doctest.h>
#include <lodepng.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "render_bridge.hpp"
#include "rml_interfaces.hpp"

namespace dwe::ui {
namespace {

// An indexed PNG of `side` x `side`, every pixel index 1.
std::vector<std::byte> indexedPng(unsigned side) {
  lodepng::State state;
  for (unsigned entry = 0; entry < 2; ++entry) {
    REQUIRE(lodepng_palette_add(&state.info_png.color, 9, 9, 9, 255) == 0);
    REQUIRE(lodepng_palette_add(&state.info_raw, 9, 9, 9, 255) == 0);
  }
  state.info_png.color.colortype = LCT_PALETTE;
  state.info_png.color.bitdepth = 8;
  state.info_raw.colortype = LCT_PALETTE;
  state.info_raw.bitdepth = 8;
  state.encoder.auto_convert = 0;
  std::vector<unsigned char> png;
  REQUIRE(lodepng::encode(png, std::vector<unsigned char>(std::size_t{side} * side, 1), side, side,
                          state) == 0);
  const auto* begin = reinterpret_cast<const std::byte*>(png.data());
  return {begin, begin + png.size()};
}

// An RGBA PNG of one colour whose green is `green`.
std::vector<std::byte> rgbaPng(unsigned width, unsigned height, unsigned char green) {
  std::vector<unsigned char> pixels;
  for (std::size_t pixel = 0; pixel < std::size_t{width} * height; ++pixel) {
    pixels.insert(pixels.end(), {10, green, 30, 255});
  }
  std::vector<unsigned char> png;
  REQUIRE(lodepng::encode(png, pixels, width, height) == 0);
  const auto* begin = reinterpret_cast<const std::byte*>(png.data());
  return {begin, begin + png.size()};
}

// Palette 1 is red: "0000 001f".
constexpr std::string_view SheetMeta =
    "mode: indexed8\npalettes:\n- \"0000 7fff\"\n- \"0000 001f\"\n";

// The extracted 4x4 sheet, a remaster's copy 4 times larger (as ui_original), and the player's
// images: palette 1 twice as large, palette 0 of another aspect ratio.
struct Packs {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;

  Packs() {
    memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
    memory.addFile("remaster/pack.yaml", "id: remaster\nversion: 0.1.0\nlayer: remaster\n");
    memory.addFile("mine/pack.yaml", "id: custom_images\nversion: 1.0.0\nlayer: user\n");
    memory.addFile("base/texture/sheet.png", indexedPng(4));
    memory.addFile("base/texture/sheet_meta.yaml", SheetMeta);
    memory.addFile("remaster/texture/sheet.png", indexedPng(16));
    memory.addFile("remaster/texture/sheet_meta.yaml", SheetMeta);
    memory.addFile("mine/texture/sheet_palette1.png", rgbaPng(8, 8, 200));
    memory.addFile("mine/texture/sheet_palette0.png", rgbaPng(8, 4, 100));
    const std::vector<std::filesystem::path> roots{"base", "remaster", "mine"};
    auto mounted = assets::VirtualFileSystem::mount(memory, roots);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
  }

  [[nodiscard]] core::Result<DrawnImage> load(std::string_view path) const {
    auto reference = parseImageReference(path);
    REQUIRE_MESSAGE(reference.has_value(), reference.error().message);
    return loadDrawnImage(*files, *reference);
  }
};

std::uint8_t greenOf(const assets::Image& image) {
  return std::to_integer<std::uint8_t>(image.pixels.at(1));
}

} // namespace

TEST_CASE("a player's image of a palette is drawn instead, at the size of the game's image") {
  const Packs packs;
  const auto drawn = packs.load("/texture/sheet.png?palette=1");
  REQUIRE_MESSAGE(drawn.has_value(), drawn.error().message);
  CHECK(drawn->pixels.width == 8);
  CHECK(drawn->pixels.height == 8);
  CHECK(greenOf(drawn->pixels) == 200);
  // The extracted image's size, not the remaster's copy above it.
  CHECK(drawn->size.width == 4);
  CHECK(drawn->size.height == 4);
  auto reference = parseImageReference("/texture/sheet?palette=1");
  REQUIRE(reference.has_value());
  const auto size = imageSize(*packs.files, *reference);
  REQUIRE(size.has_value());
  CHECK(size->width == 4);
  CHECK(size->height == 4);
}

TEST_CASE("a player's image of another aspect ratio is left out: the game's is drawn") {
  const Packs packs;
  const auto drawn = packs.load("/texture/sheet.png?palette=0");
  REQUIRE_MESSAGE(drawn.has_value(), drawn.error().message);
  // The remaster's copy, with palette 0 (white), at its own size as before.
  CHECK(drawn->pixels.width == 16);
  CHECK(drawn->size.width == 16);
  CHECK(greenOf(drawn->pixels) == 255);
  // An image without a palette is its palette 0: the same image is left out.
  const auto plain = packs.load("/texture/sheet.png");
  REQUIRE(plain.has_value());
  CHECK(plain->pixels.width == 16);
}

TEST_CASE("a palette with no player's image is drawn from the game's images, the tone on top") {
  Packs packs;
  packs.memory.removeFile("mine/texture/sheet_palette1.png");
  const std::vector<std::filesystem::path> roots{"base", "remaster", "mine"};
  auto mounted = assets::VirtualFileSystem::mount(packs.memory, roots);
  REQUIRE(mounted.has_value());
  auto reference = parseImageReference("/texture/sheet?palette=1&tone=grey");
  REQUIRE(reference.has_value());
  const auto drawn = loadDrawnImage(*mounted, *reference);
  REQUIRE(drawn.has_value());
  CHECK(drawn->pixels.width == 16);
  // A player's image takes the tone too.
  const Packs withImage;
  const auto toned = withImage.load("/texture/sheet?palette=1&tone=grey");
  REQUIRE(toned.has_value());
  CHECK(toned->pixels.width == 8);
  CHECK(greenOf(toned->pixels) != 200);
}

TEST_CASE(
    "RmlUi lays out a player's image at the game image's size; the texture keeps its pixels") {
  const Packs packs;
  TextureSources sources{*packs.files};
  RenderBridge bridge{sources, nullptr};
  Rml::Vector2i size;
  const Rml::TextureHandle texture = bridge.LoadTexture(size, "/texture/sheet.png?palette=1");
  REQUIRE(texture != 0);
  CHECK(size == Rml::Vector2i{4, 4});
  // An <img rect>, a sprite and a decorator are measured in those 4 x 4 pixels.
  CHECK(bridge.drawsSharp(texture));
}

} // namespace dwe::ui
