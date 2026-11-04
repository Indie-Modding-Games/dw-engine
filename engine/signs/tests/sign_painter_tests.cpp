#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/signs/map_signs.hpp>
#include <dwe/signs/sign_font.hpp>
#include <dwe/signs/sign_painter.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace dwe::signs;
using dwe::assets::IndexedImage;
using dwe::assets::MapImages;
using dwe::assets::PaletteColor;

namespace {

std::vector<std::byte> fontFile(std::string_view name) {
  const std::filesystem::path path =
      std::filesystem::path{DWE_GAME_PACK} / "ui" / "fonts" / std::format("{}.ttf", name);
  std::ifstream stream{path, std::ios::binary};
  REQUIRE_MESSAGE(stream.good(), path.string());
  std::vector<char> bytes{std::istreambuf_iterator<char>{stream}, {}};
  std::vector<std::byte> file(bytes.size());
  std::ranges::transform(bytes, file.begin(), [](char c) { return static_cast<std::byte>(c); });
  return file;
}

const SignFont& boldFont() {
  static const SignFont font = [] {
    auto loaded = SignFont::load(fontFile("noto_sans_bold"));
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
    return std::move(*loaded);
  }();
  return font;
}

// Black 0, white 1, red 2, green 3, then a ramp of greys 4..19 (16 steps), and a transparent 20.
std::vector<PaletteColor> testPalette() {
  std::vector<PaletteColor> palette{
      {.red = 0, .green = 0, .blue = 0, .alpha = 255},
      {.red = 255, .green = 255, .blue = 255, .alpha = 255},
      {.red = 255, .green = 0, .blue = 0, .alpha = 255},
      {.red = 0, .green = 255, .blue = 0, .alpha = 255},
  };
  for (int step = 0; step < 16; ++step) {
    const auto grey = static_cast<std::uint8_t>(step * 17);
    palette.push_back({.red = grey, .green = grey, .blue = grey, .alpha = 255});
  }
  palette.push_back({.red = 9, .green = 9, .blue = 9, .alpha = 0});
  return palette;
}

IndexedImage filled(std::uint32_t width, std::uint32_t height, std::uint8_t index) {
  return {.width = width,
          .height = height,
          .indices = std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height, index),
          .palette = {}};
}

std::uint8_t pixel(const IndexedImage& image, std::int32_t x, std::int32_t y) {
  return image.indices[(static_cast<std::size_t>(y) * image.width) + static_cast<std::size_t>(x)];
}

std::size_t countOf(const IndexedImage& image, std::uint8_t index) {
  return static_cast<std::size_t>(std::ranges::count(image.indices, index));
}

// The bounding box of the pixels with an index.
PixelRect inkOf(const IndexedImage& image, std::uint8_t index) {
  PixelRect box{};
  bool first = true;
  for (std::int32_t y = 0; std::cmp_less(y, image.height); ++y) {
    for (std::int32_t x = 0; std::cmp_less(x, image.width); ++x) {
      if (pixel(image, x, y) != index) {
        continue;
      }
      if (first) {
        box = {.x = x, .y = y, .width = 1, .height = 1};
        first = false;
        continue;
      }
      const std::int32_t right = std::max(box.x + box.width, x + 1);
      const std::int32_t bottom = std::max(box.y + box.height, y + 1);
      box.x = std::min(box.x, x);
      box.y = std::min(box.y, y);
      box.width = right - box.x;
      box.height = bottom - box.y;
    }
  }
  return box;
}

Sign textSign(PixelRect region) {
  Sign sign;
  sign.id = "test";
  sign.region = region;
  sign.clear.mode = SignClear::Mode::None;
  sign.size = 12.0F;
  sign.color = {.red = 255, .green = 255, .blue = 255, .alpha = 255};
  return sign;
}

} // namespace

TEST_CASE("inpainting clears a sign's letters with the shading around them") {
  const std::vector<PaletteColor> palette = testPalette();
  // A board that goes from black on the left to white on the right, with red letters on it.
  IndexedImage image = filled(20, 10, 0);
  for (std::int32_t y = 0; y < 10; ++y) {
    for (std::int32_t x = 10; x < 20; ++x) {
      image.indices[(static_cast<std::size_t>(y) * 20) + static_cast<std::size_t>(x)] = 1;
    }
  }
  for (std::int32_t x = 4; x < 16; ++x) {
    image.indices[(std::size_t{5} * 20) + static_cast<std::size_t>(x)] = 2;
  }
  Sign sign = textSign({.x = 3, .y = 3, .width = 14, .height = 4});
  sign.clear.mode = SignClear::Mode::Inpaint;

  const PixelRect changed = paintSign(image, palette, sign, "", boldFont());

  CHECK(changed == PixelRect{.x = 3, .y = 3, .width = 14, .height = 4});
  CHECK(countOf(image, 2) == 0);
  // Nothing outside the rectangle changes.
  CHECK(pixel(image, 2, 5) == 0);
  CHECK(pixel(image, 17, 5) == 1);
  // Across the board the cleared pixels go from dark to light, as the board around them does.
  const auto brightness = [&](std::int32_t x) { return palette[pixel(image, x, 5)].red; };
  CHECK(brightness(6) < brightness(9));
  CHECK(brightness(9) <= brightness(13));
  CHECK(brightness(6) > 0);
  CHECK(brightness(3) < 40);
  CHECK(brightness(16) > 200);
}

TEST_CASE("a fill clears with the colour around the rectangle or the one given") {
  const std::vector<PaletteColor> palette = testPalette();
  IndexedImage image = filled(12, 8, 3);
  for (std::int32_t x = 3; x < 9; ++x) {
    image.indices[(std::size_t{4} * 12) + static_cast<std::size_t>(x)] = 2;
  }
  Sign sign = textSign({.x = 2, .y = 2, .width = 8, .height = 4});
  sign.clear.mode = SignClear::Mode::Fill;
  IndexedImage sampled = image;
  paintSign(sampled, palette, sign, "", boldFont());
  CHECK(countOf(sampled, 2) == 0);
  CHECK(countOf(sampled, 3) == 12 * 8);

  sign.clear.color = PaletteColor{.red = 250, .green = 250, .blue = 250, .alpha = 255};
  IndexedImage given = image;
  paintSign(given, palette, sign, "", boldFont());
  CHECK(countOf(given, 1) == 8 * 4);
}

TEST_CASE("a sign's text is drawn in its region with palette colours only") {
  const std::vector<PaletteColor> palette = testPalette();
  IndexedImage image = filled(64, 32, 0);
  const PixelRect region{.x = 8, .y = 4, .width = 48, .height = 24};
  const Sign sign = textSign(region);

  const PixelRect changed = paintSign(image, palette, sign, "OPEN", boldFont());

  REQUIRE(changed.width > 0);
  CHECK(changed.x >= region.x);
  CHECK(changed.y >= region.y);
  CHECK(changed.x + changed.width <= region.x + region.width);
  CHECK(changed.y + changed.height <= region.y + region.height);
  // Solid strokes in white, antialiased edges in greys of the palette, never the transparent one.
  CHECK(countOf(image, 1) > 20);
  CHECK(countOf(image, 20) == 0);
  CHECK(countOf(image, 2) == 0);
  // Centred: the ink's middle is near the region's.
  const PixelRect ink = inkOf(image, 1);
  CHECK(std::abs((ink.x + (ink.width / 2)) - 32) <= 2);
  CHECK(std::abs((ink.y + (ink.height / 2)) - 16) <= 2);
}

TEST_CASE("a long text shrinks to fit its region unless fit is off") {
  const std::vector<PaletteColor> palette = testPalette();
  const PixelRect region{.x = 4, .y = 4, .width = 40, .height = 16};
  Sign sign = textSign(region);
  sign.size = 14.0F;
  sign.clear.mode = SignClear::Mode::None;
  IndexedImage fitted = filled(80, 24, 0);
  paintSign(fitted, palette, sign, "RECYCLING", boldFont());
  const PixelRect fittedInk = inkOf(fitted, 1);
  CHECK(fittedInk.x >= region.x);
  CHECK(fittedInk.x + fittedInk.width <= region.x + region.width);
  CHECK(fittedInk.width >= 30);

  sign.fit = false;
  IndexedImage cut = filled(80, 24, 0);
  paintSign(cut, palette, sign, "RECYCLING", boldFont());
  // The text runs past both ends of the region, which clips it.
  const PixelRect cutInk = inkOf(cut, 1);
  CHECK(cutInk.x == region.x);
  CHECK(cutInk.x + cutInk.width == region.x + region.width);
}

TEST_CASE("rotation, vertical text and corners turn the text") {
  const std::vector<PaletteColor> palette = testPalette();
  const PixelRect region{.x = 0, .y = 0, .width = 48, .height = 48};
  Sign flat = textSign(region);
  flat.size = 10.0F;
  IndexedImage across = filled(48, 48, 0);
  paintSign(across, palette, flat, "SALE", boldFont());
  const PixelRect acrossInk = inkOf(across, 1);
  CHECK(acrossInk.width > acrossInk.height);

  Sign turned = flat;
  turned.rotation = 90.0F;
  IndexedImage rotated = filled(48, 48, 0);
  paintSign(rotated, palette, turned, "SALE", boldFont());
  const PixelRect rotatedInk = inkOf(rotated, 1);
  CHECK(rotatedInk.height > rotatedInk.width);

  Sign stacked = flat;
  stacked.vertical = true;
  IndexedImage vertical = filled(48, 48, 0);
  paintSign(vertical, palette, stacked, "SALE", boldFont());
  const PixelRect verticalInk = inkOf(vertical, 1);
  CHECK(verticalInk.height > verticalInk.width * 2);

  // Corners that mirror the box draw the text mirrored: its ink moves to the other side.
  Sign quad = flat;
  quad.align = HorizontalAlign::Left;
  quad.fit = false;
  quad.size = 8.0F;
  IndexedImage left = filled(48, 48, 0);
  paintSign(left, palette, quad, "AB", boldFont());
  quad.corners =
      std::array<PixelPoint, 4>{PixelPoint{.x = 48, .y = 0}, PixelPoint{.x = 0, .y = 0},
                                PixelPoint{.x = 0, .y = 48}, PixelPoint{.x = 48, .y = 48}};
  IndexedImage mirrored = filled(48, 48, 0);
  paintSign(mirrored, palette, quad, "AB", boldFont());
  CHECK(inkOf(left, 1).x < 8);
  CHECK(inkOf(mirrored, 1).x + inkOf(mirrored, 1).width > 40);
}

TEST_CASE("an outline and a glow surround the letters, region colours keep to the original's") {
  const std::vector<PaletteColor> palette = testPalette();
  const PixelRect region{.x = 0, .y = 0, .width = 48, .height = 24};
  Sign neon = textSign(region);
  neon.size = 12.0F;
  neon.color = {.red = 255, .green = 255, .blue = 255, .alpha = 255};
  neon.outline =
      SignOutline{.color = {.red = 255, .green = 0, .blue = 0, .alpha = 255}, .width = 1};
  neon.glow = SignGlow{
      .color = {.red = 0, .green = 255, .blue = 0, .alpha = 255}, .radius = 2, .strength = 1};
  IndexedImage image = filled(48, 24, 0);
  paintSign(image, palette, neon, "A", boldFont());
  CHECK(countOf(image, 1) > 0);
  CHECK(countOf(image, 2) > 0);
  CHECK(countOf(image, 3) > 0);

  // With region colours, only black (all the region has) and the white of one pixel may appear.
  IndexedImage limited = filled(48, 24, 0);
  limited.indices[0] = 1;
  neon.colors = SignColors::Region;
  paintSign(limited, palette, neon, "A", boldFont());
  CHECK(std::ranges::all_of(limited.indices, [](std::uint8_t index) { return index <= 1; }));
}

TEST_CASE("painted signs come back as patches that give the same images again") {
  MapImages images;
  images.background = filled(64, 32, 0);
  images.backgroundPalettes = {testPalette()};
  dwe::assets::MapTexturePage page{
      .bitsPerPixel = 8,
      .vramX =
          dwe::assets::MapVramLayout::FirstPageX + dwe::assets::MapVramLayout::EightBitPageWidth,
      .vramY = 0,
      .clutRow = 0,
      .image = filled(256, 256, 0),
      .cluts = {},
  };
  images.pages.push_back(page);

  std::vector<Sign> signs;
  Sign onBackground = textSign({.x = 4, .y = 4, .width = 40, .height = 20});
  onBackground.id = "background";
  onBackground.text = "signs.open";
  signs.push_back(onBackground);
  Sign onPage = textSign({.x = 10, .y = 10, .width = 40, .height = 20});
  onPage.id = "page";
  onPage.text = "signs.sale";
  onPage.target = {.kind = SignTarget::Kind::Page, .bitsPerPixel = 8, .page = 1, .clut = -1};
  signs.push_back(onPage);
  Sign missing = onPage;
  missing.id = "missing";
  missing.target.page = 5;
  signs.push_back(missing);

  const SignResources resources{
      .text = [](std::string_view key) { return key == "signs.open" ? "OPEN" : "SALE"; },
      .font = [](std::string_view) { return &boldFont(); },
  };
  const PaintedSigns painted = paintSigns(images, signs, resources);
  REQUIRE(painted.patches.size() == 2);
  REQUIRE(painted.warnings.size() == 1);
  CHECK(painted.warnings.front().contains("page 5"));

  MapImages patched = images;
  applySignPatches(patched, painted.patches, {});
  CHECK(countOf(patched.background, 1) > 0);
  CHECK(countOf(patched.pages.front().image, 1) > 0);
  // Applying again over the patched images changes nothing.
  MapImages again = patched;
  applySignPatches(again, painted.patches, {});
  CHECK(again.background.indices == patched.background.indices);
  CHECK(again.pages.front().image.indices == patched.pages.front().image.indices);
}

TEST_CASE("a background sign is painted into the sprites cut from the background over it") {
  MapImages images;
  images.background = filled(64, 32, 0);
  images.backgroundPalettes = {testPalette()};
  dwe::assets::MapTexturePage page{
      .bitsPerPixel = 8,
      .vramX = dwe::assets::MapVramLayout::FirstPageX,
      .vramY = 0,
      .clutRow = 0,
      .image = filled(256, 256, 0),
      .cluts = {},
  };
  // The sprite copies the background but for one texel of its own (an object in front).
  constexpr std::int32_t TexX = 16;
  constexpr std::int32_t TexY = 8;
  page.image.indices[((TexY + 10) * 256) + TexX + 20] = 3;
  images.pages.push_back(page);
  dwe::assets::MapObjects objects;
  objects.sprites.push_back({.texX = TexX,
                             .texY = TexY,
                             .width = 40,
                             .height = 20,
                             .position = {},
                             .clut = -1,
                             .transparency = 0});
  objects.instances.push_back({.frames = {{.sprite = 0, .time = 0}}, .x = 4, .y = 4, .flag = 0});

  const std::vector<Sign> signs{textSign({.x = 4, .y = 4, .width = 40, .height = 20})};
  const SignResources resources{
      .text = [](std::string_view) { return std::string{"OPEN"}; },
      .font = [](std::string_view) { return &boldFont(); },
  };
  CHECK(paintSigns(images, signs, resources).patches.size() == 1);
  const PaintedSigns painted = paintSigns(images, signs, resources, &objects);
  REQUIRE(painted.patches.size() == 2);
  CHECK(painted.patches.back().kind == SignTarget::Kind::Page);

  MapImages patched = images;
  applySignPatches(patched, painted.patches, objects.sprites);
  CHECK(countOf(patched.background, 1) > 0);
  for (std::int32_t y = 0; y < 20; ++y) {
    for (std::int32_t x = 0; x < 40; ++x) {
      const std::uint8_t texel = pixel(patched.pages.front().image, TexX + x, TexY + y);
      if (x == 20 && y == 10) {
        CHECK(texel == 3);
      } else {
        CHECK(texel == pixel(patched.background, 4 + x, 4 + y));
      }
    }
  }
}

TEST_CASE("a sprite cut a few pixels off its instance still gets the sign where it came from") {
  MapImages images;
  // A background of scattered greys, so only one shift lines the sprite up with it.
  images.background = filled(64, 32, 0);
  for (std::size_t index = 0; index < images.background.indices.size(); ++index) {
    images.background.indices[index] =
        static_cast<std::uint8_t>(4 + (((index * 2654435761U) >> 7U) % 16));
  }
  images.backgroundPalettes = {testPalette()};
  dwe::assets::MapTexturePage page{
      .bitsPerPixel = 8,
      .vramX = dwe::assets::MapVramLayout::FirstPageX,
      .vramY = 0,
      .clutRow = 0,
      .image = filled(256, 256, 0),
      .cluts = {},
  };
  // The texels came from the background two pixels right of where the instance draws them.
  for (std::int32_t y = 0; y < 20; ++y) {
    for (std::int32_t x = 0; x < 40; ++x) {
      page.image.indices[(static_cast<std::size_t>(y) * 256) + static_cast<std::size_t>(x)] =
          pixel(images.background, 6 + x, 4 + y);
    }
  }
  images.pages.push_back(page);
  dwe::assets::MapObjects objects;
  objects.sprites.push_back({.texX = 0,
                             .texY = 0,
                             .width = 40,
                             .height = 20,
                             .position = {},
                             .clut = -1,
                             .transparency = 0});
  objects.instances.push_back({.frames = {{.sprite = 0, .time = 0}}, .x = 4, .y = 4, .flag = 0});

  Sign sign = textSign({.x = 8, .y = 6, .width = 30, .height = 14});
  sign.clear.mode = SignClear::Mode::Fill;
  sign.clear.color = PaletteColor{.red = 0, .green = 0, .blue = 0, .alpha = 255};
  const SignResources resources{
      .text = [](std::string_view) { return std::string{"OPEN"}; },
      .font = [](std::string_view) { return &boldFont(); },
  };
  const PaintedSigns painted = paintSigns(images, std::vector<Sign>{sign}, resources, &objects);
  REQUIRE(painted.patches.size() == 2);
  MapImages patched = images;
  applySignPatches(patched, painted.patches, objects.sprites);
  for (std::int32_t y = 0; y < 20; ++y) {
    for (std::int32_t x = 0; x < 40; ++x) {
      CHECK(pixel(patched.pages.front().image, x, y) == pixel(patched.background, 6 + x, 4 + y));
    }
  }
}

TEST_CASE("map signs read the packs' overlays per language and keep the painting") {
  dwe::assets::MemoryFileSystem memory;
  memory.addFile("game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("game/ui/fonts/noto_sans_bold.ttf", fontFile("noto_sans_bold"));
  memory.addFile("game/signs/test01.yaml",
                 "signs:\n  - {id: neon, region: [0, 0, 32, 16], text: signs.neon, clear: none}\n");
  memory.addFile("game/signs/es/test01.yaml",
                 "signs:\n  - {id: mat, region: [0, 16, 32, 16], text: signs.mat, clear: none}\n");
  auto files = dwe::assets::VirtualFileSystem::mount(memory, {"game"});
  REQUIRE_MESSAGE(files.has_value(), files.error().message);

  MapSigns signs{*files};
  CHECK(signs.overlay("test01", "en").signs.size() == 1);
  CHECK(signs.overlay("test01", "es").signs.size() == 2);
  CHECK(signs.overlay("other", "es").signs.empty());

  MapImages images;
  images.background = filled(32, 32, 0);
  images.backgroundPalettes = {testPalette()};
  int asked = 0;
  std::string language = "en";
  const auto text = [&](std::string_view key) {
    ++asked;
    if (key == "signs.neon") {
      return std::string{language == "es" ? "COLISEO" : "ARENA"};
    }
    return std::string{"BIENVENIDO"};
  };
  const PaintedSigns& english = signs.painted(images, nullptr, "test01", "en", 0, text);
  CHECK(english.patches.size() == 1);
  CHECK(asked == 1);
  signs.painted(images, nullptr, "test01", "en", 0, text);
  CHECK(asked == 1);

  language = "es";
  const PaintedSigns& spanish = signs.painted(images, nullptr, "test01", "es", 0, text);
  CHECK(spanish.patches.size() == 2);
  CHECK(asked == 3);
  // New texts of the same language (another revision) paint again.
  signs.painted(images, nullptr, "test01", "es", 1, text);
  CHECK(asked == 5);
}

TEST_CASE("a diffuse clear removes only what differs from the board, inside its quad") {
  const std::vector<PaletteColor> palette = testPalette();
  // A dark grey board (index 6) with a green frame (3) around it and white letters (1) on it.
  IndexedImage image = filled(24, 16, 3);
  for (std::int32_t y = 2; y < 14; ++y) {
    for (std::int32_t x = 2; x < 22; ++x) {
      image.indices[(static_cast<std::size_t>(y) * 24) + static_cast<std::size_t>(x)] = 6;
    }
  }
  for (std::int32_t x = 6; x < 18; ++x) {
    image.indices[(std::size_t{7} * 24) + static_cast<std::size_t>(x)] = 1;
    image.indices[(std::size_t{8} * 24) + static_cast<std::size_t>(x)] = 1;
  }
  Sign sign = textSign({.x = 0, .y = 0, .width = 24, .height = 16});
  sign.clear.mode = SignClear::Mode::Diffuse;
  // The quad keeps clear of the frame's left and right sides.
  sign.clear.quad =
      std::array<PixelPoint, 4>{PixelPoint{.x = 2, .y = 0}, PixelPoint{.x = 22, .y = 0},
                                PixelPoint{.x = 22, .y = 16}, PixelPoint{.x = 2, .y = 16}};
  sign.clear.grow = 0;

  paintSign(image, palette, sign, "", boldFont());

  CHECK(countOf(image, 1) == 0);
  // The letters became board; the frame's sides, outside the quad, stay.
  CHECK(pixel(image, 10, 7) == 6);
  CHECK(pixel(image, 0, 7) == 3);
  CHECK(pixel(image, 23, 7) == 3);
}

TEST_CASE("a transparent clear lets the sprite's backdrop show and draws hard-edged letters") {
  const std::vector<PaletteColor> palette = testPalette();
  // A lit sprite: old red lettering on a see-through page.
  IndexedImage image = filled(64, 32, 20);
  for (std::int32_t x = 10; x < 54; ++x) {
    image.indices[(std::size_t{16} * 64) + static_cast<std::size_t>(x)] = 2;
  }
  Sign sign = textSign({.x = 8, .y = 4, .width = 48, .height = 24});
  sign.clear.mode = SignClear::Mode::Transparent;
  sign.colors = SignColors::All;

  IndexedImage cleared = image;
  paintSign(cleared, palette, sign, "", boldFont());
  CHECK(countOf(cleared, 2) == 0);
  CHECK(countOf(cleared, 20) == 64 * 32);

  IndexedImage written = image;
  paintSign(written, palette, sign, "LIT", boldFont());
  CHECK(countOf(written, 2) == 0);
  // Letters in the sign's colour with no greys blended at their edges; the rest stays see-through.
  CHECK(countOf(written, 1) > 20);
  CHECK(countOf(written, 1) + countOf(written, 20) == 64 * 32);
}
