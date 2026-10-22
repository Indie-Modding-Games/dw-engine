#include <dwe/formats/image/tim.hpp>

#include <doctest/doctest.h>

#include <vector>

#include "tim_builder.hpp"

using namespace dwe::formats::image;
using dwe::formats::image::test::buildTim;
using dwe::formats::image::test::bytesOf;
using dwe::formats::image::test::directTim;
using dwe::formats::image::test::indexedTim;
using dwe::formats::image::test::TimSpec;

namespace {

constexpr Color15 Red = 0x001F;
constexpr Color15 Green = 0x03E0;
constexpr Color15 Blue = 0x7C00;
constexpr Color15 White = 0x7FFF;
constexpr Color15 StpBlack = 0x8000;

constexpr Rgba8 RedRgba{.r = 255, .g = 0, .b = 0, .a = 255};
constexpr Rgba8 GreenRgba{.r = 0, .g = 255, .b = 0, .a = 255};
constexpr Rgba8 BlueRgba{.r = 0, .g = 0, .b = 255, .a = 255};
constexpr Rgba8 Transparent{.r = 0, .g = 0, .b = 0, .a = 0};

// A 16-colour CLUT whose entry i is gray level i (0 stays transparent black).
std::vector<Color15> grayClut(std::size_t size) {
  std::vector<Color15> clut;
  for (std::size_t i = 0; i < size; ++i) {
    const auto level = static_cast<Color15>(i % 32);
    clut.push_back(static_cast<Color15>(level | (level << 5U) | (level << 10U)));
  }
  return clut;
}

} // namespace

TEST_CASE("console colours widen to 8 bits and only 0x0000 is transparent") {
  CHECK(toRgba8(Red) == RedRgba);
  CHECK(toRgba8(Green) == GreenRgba);
  CHECK(toRgba8(Blue) == BlueRgba);
  CHECK(toRgba8(White) == Rgba8{.r = 255, .g = 255, .b = 255, .a = 255});
  CHECK(toRgba8(0x0001) == Rgba8{.r = 8, .g = 0, .b = 0, .a = 255});
  CHECK(toRgba8(0x0000) == Transparent);
  CHECK(toRgba8(StpBlack) == Rgba8{.r = 0, .g = 0, .b = 0, .a = 255});
  CHECK(toRgba8(static_cast<Color15>(Red | 0x8000U)) == RedRgba);
}

TEST_CASE("a 16-bit TIM without CLUT decodes its colours directly") {
  const auto file =
      buildTim(directTim(TimPixelMode::Direct16, {.x = 320, .y = 0, .width = 2, .height = 2},
                         bytesOf({0x1F, 0x00, 0xE0, 0x03, 0x00, 0x7C, 0x00, 0x00})));
  const auto tim = readTim(file);
  REQUIRE(tim.has_value());
  CHECK(tim->mode == TimPixelMode::Direct16);
  CHECK_FALSE(tim->hasClut);
  CHECK(tim->imageRect == VramRect{.x = 320, .y = 0, .width = 2, .height = 2});
  CHECK(tim->byteSize == file.size());
  CHECK(timWidth(*tim) == 2);
  CHECK(timPaletteCount(*tim) == 0);
  CHECK_FALSE(timIndices(*tim).has_value());

  const auto image = decodeTim(*tim);
  REQUIRE(image.has_value());
  CHECK(image->width == 2);
  CHECK(image->height == 2);
  CHECK(image->pixels == std::vector{RedRgba, GreenRgba, BlueRgba, Transparent});
}

TEST_CASE("a 24-bit TIM decodes RGB triplets and skips row padding") {
  // Two 16-bit units per row hold one 3-byte pixel and one byte of padding.
  const auto file =
      buildTim(directTim(TimPixelMode::Direct24, {.x = 0, .y = 0, .width = 2, .height = 2},
                         bytesOf({1, 2, 3, 0xEE, 4, 5, 6, 0xEE})));
  const auto tim = readTim(file);
  REQUIRE(tim.has_value());
  CHECK(timWidth(*tim) == 1);
  const auto image = decodeTim(*tim);
  REQUIRE(image.has_value());
  CHECK(image->pixels == std::vector{Rgba8{.r = 1, .g = 2, .b = 3, .a = 255},
                                     Rgba8{.r = 4, .g = 5, .b = 6, .a = 255}});
}

TEST_CASE("a 4-bit TIM with a CLUT exposes indices, palette and RGBA") {
  // One 16-bit unit is four pixels; the first pixel is in the low nibble.
  const auto file = buildTim(indexedTim(
      TimPixelMode::Indexed4, VramRect{.x = 0, .y = 480, .width = 16, .height = 1}, grayClut(16),
      {.x = 640, .y = 0, .width = 1, .height = 2}, bytesOf({0x10, 0x32, 0x54, 0xF6})));
  const auto tim = readTim(file);
  REQUIRE(tim.has_value());
  CHECK(tim->hasClut);
  CHECK(tim->clutRect == VramRect{.x = 0, .y = 480, .width = 16, .height = 1});
  CHECK(tim->clut == grayClut(16));
  CHECK(timWidth(*tim) == 4);
  CHECK(timPaletteCount(*tim) == 1);

  const auto indices = timIndices(*tim);
  REQUIRE(indices.has_value());
  CHECK(*indices == std::vector<std::uint8_t>{0, 1, 2, 3, 4, 5, 6, 15});

  const auto image = decodeTim(*tim);
  REQUIRE(image.has_value());
  CHECK(image->width == 4);
  CHECK(image->height == 2);
  CHECK(image->pixels[0] == Transparent);
  CHECK(image->pixels[1] == toRgba8(grayClut(16)[1]));
  CHECK(image->pixels[7] == toRgba8(grayClut(16)[15]));
}

TEST_CASE("an 8-bit TIM with a CLUT decodes through its palette") {
  std::vector<Color15> clut(256, White);
  clut[0] = 0;
  clut[200] = Red;
  const auto file = buildTim(
      indexedTim(TimPixelMode::Indexed8, VramRect{.x = 0, .y = 500, .width = 256, .height = 1},
                 clut, {.x = 0, .y = 0, .width = 1, .height = 1}, bytesOf({200, 0})));
  const auto tim = readTim(file);
  REQUIRE(tim.has_value());
  CHECK(timWidth(*tim) == 2);
  CHECK(timPaletteCount(*tim) == 1);
  const auto image = decodeTim(*tim);
  REQUIRE(image.has_value());
  CHECK(image->pixels == std::vector{RedRgba, Transparent});
}

TEST_CASE("indexed TIMs without a CLUT keep their indices but cannot be decoded to RGBA") {
  for (const auto mode : {TimPixelMode::Indexed4, TimPixelMode::Indexed8}) {
    const auto file =
        buildTim(directTim(mode, {.x = 0, .y = 0, .width = 1, .height = 1}, bytesOf({0x21, 0x43})));
    const auto tim = readTim(file);
    REQUIRE(tim.has_value());
    CHECK(timPaletteCount(*tim) == 0);
    CHECK(timIndices(*tim).has_value());
    CHECK_FALSE(decodeTim(*tim).has_value());
  }
}

TEST_CASE("a TIM with several palettes decodes with the chosen one") {
  // Two rows of 16 colours, and a 32-wide row that holds two more 16-colour palettes side by side.
  std::vector<Color15> clut(32, 0);
  clut[1] = Red;
  clut[16 + 1] = Green;
  const auto rows = buildTim(
      indexedTim(TimPixelMode::Indexed4, VramRect{.x = 0, .y = 480, .width = 16, .height = 2}, clut,
                 {.x = 0, .y = 0, .width = 1, .height = 1}, bytesOf({0x11, 0x11})));
  const auto byRow = readTim(rows);
  REQUIRE(byRow.has_value());
  CHECK(timPaletteCount(*byRow) == 2);
  CHECK(decodeTim(*byRow, 0)->pixels[0] == RedRgba);
  CHECK(decodeTim(*byRow, 1)->pixels[0] == GreenRgba);
  CHECK_FALSE(decodeTim(*byRow, 2).has_value());
  CHECK_FALSE(timPalette(*byRow, 2).has_value());

  const auto wide = buildTim(
      indexedTim(TimPixelMode::Indexed4, VramRect{.x = 0, .y = 480, .width = 32, .height = 1}, clut,
                 {.x = 0, .y = 0, .width = 1, .height = 1}, bytesOf({0x11, 0x11})));
  const auto byColumn = readTim(wide);
  REQUIRE(byColumn.has_value());
  CHECK(timPaletteCount(*byColumn) == 2);
  CHECK((*timPalette(*byColumn, 1))[1] == Green);
}

TEST_CASE("an 8-bit TIM with a short CLUT rejects indices past its end") {
  const auto file = buildTim(
      indexedTim(TimPixelMode::Indexed8, VramRect{.x = 0, .y = 480, .width = 16, .height = 1},
                 grayClut(16), {.x = 0, .y = 0, .width = 1, .height = 1}, bytesOf({15, 16})));
  const auto tim = readTim(file);
  REQUIRE(tim.has_value());
  CHECK(timPaletteCount(*tim) == 1);
  CHECK_FALSE(decodeTim(*tim).has_value());
}

TEST_CASE("bytes after the TIM are ignored") {
  auto file = buildTim(directTim(TimPixelMode::Direct16, {.x = 0, .y = 0, .width = 1, .height = 1},
                                 bytesOf({1, 2})));
  const std::size_t size = file.size();
  file.resize(size + 64);
  const auto tim = readTim(file);
  REQUIRE(tim.has_value());
  CHECK(tim->byteSize == size);
}

TEST_CASE("malformed TIMs are rejected") {
  const TimSpec valid =
      indexedTim(TimPixelMode::Indexed4, VramRect{.x = 0, .y = 480, .width = 16, .height = 1},
                 grayClut(16), {.x = 0, .y = 0, .width = 1, .height = 1}, bytesOf({0, 0}));
  REQUIRE(readTim(buildTim(valid)).has_value());

  SUBCASE("empty or truncated header") {
    CHECK_FALSE(readTim({}).has_value());
    const auto file = buildTim(valid);
    CHECK_FALSE(readTim(std::span{file}.first(7)).has_value());
    CHECK_FALSE(readTim(std::span{file}.first(12)).has_value());
  }
  SUBCASE("truncated data") {
    const auto file = buildTim(valid);
    CHECK_FALSE(readTim(std::span{file}.first(file.size() - 1)).has_value());
  }
  SUBCASE("bad magic") {
    auto file = buildTim(valid);
    file[0] = std::byte{0x11};
    CHECK_FALSE(readTim(file).has_value());
  }
  SUBCASE("mixed or unknown pixel mode") {
    auto file = buildTim(valid);
    file[4] = std::byte{0x0C};
    CHECK_FALSE(readTim(file).has_value());
  }
  SUBCASE("block length past the end") {
    auto spec = valid;
    spec.imageLength = 0xFFFFFFF0;
    CHECK_FALSE(readTim(buildTim(spec)).has_value());
    spec = valid;
    spec.clutLength = 0x10000;
    CHECK_FALSE(readTim(buildTim(spec)).has_value());
  }
  SUBCASE("block length smaller than its rectangle") {
    auto spec = valid;
    spec.imageLength = 12;
    CHECK_FALSE(readTim(buildTim(spec)).has_value());
    spec = valid;
    spec.clutLength = 4;
    CHECK_FALSE(readTim(buildTim(spec)).has_value());
  }
  SUBCASE("rectangle empty or bigger than video memory") {
    auto spec = valid;
    spec.imageRect.width = 0;
    CHECK_FALSE(readTim(buildTim(spec)).has_value());
    spec = valid;
    spec.imageRect.height = 513;
    CHECK_FALSE(readTim(buildTim(spec)).has_value());
    spec = valid;
    spec.clutRect = VramRect{.x = 0, .y = 480, .width = 1025, .height = 1};
    CHECK_FALSE(readTim(buildTim(spec)).has_value());
  }
}
