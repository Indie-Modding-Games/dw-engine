#include <dwe/formats/image/png.hpp>

#include <doctest/doctest.h>
#include <lodepng.h>

#include <algorithm>
#include <string_view>
#include <vector>

using namespace dwe::formats::image;

namespace {

std::vector<unsigned char> toUnsignedChars(const std::vector<std::byte>& bytes) {
  std::vector<unsigned char> out(bytes.size());
  std::ranges::transform(bytes, out.begin(),
                         [](std::byte value) { return std::to_integer<unsigned char>(value); });
  return out;
}

bool hasChunk(const std::vector<std::byte>& png, std::string_view type) {
  const auto chars = toUnsignedChars(png);
  return std::ranges::search(chars, type).begin() != chars.end();
}

RgbaImage sampleImage() {
  RgbaImage image{.width = 3, .height = 2, .pixels = {}};
  for (std::uint8_t i = 0; i < 6; ++i) {
    image.pixels.push_back(Rgba8{.r = static_cast<std::uint8_t>(i * 40),
                                 .g = static_cast<std::uint8_t>(255 - (i * 40)),
                                 .b = 7,
                                 .a = static_cast<std::uint8_t>(i == 0 ? 0 : 255)});
  }
  return image;
}

} // namespace

TEST_CASE("an RGBA PNG decodes back to the same pixels") {
  const RgbaImage image = sampleImage();
  const auto png = encodePngRgba(image);
  REQUIRE(png.has_value());

  lodepng::State state;
  std::vector<unsigned char> decoded;
  unsigned width = 0;
  unsigned height = 0;
  REQUIRE(lodepng::decode(decoded, width, height, state, toUnsignedChars(*png)) == 0);
  CHECK(state.info_png.color.colortype == LCT_RGBA);
  CHECK(state.info_png.color.bitdepth == 8);
  CHECK(width == 3);
  CHECK(height == 2);
  REQUIRE(decoded.size() == image.pixels.size() * 4);
  for (std::size_t i = 0; i < image.pixels.size(); ++i) {
    const Rgba8 pixel{.r = decoded[i * 4],
                      .g = decoded[(i * 4) + 1],
                      .b = decoded[(i * 4) + 2],
                      .a = decoded[(i * 4) + 3]};
    CHECK(pixel == image.pixels[i]);
  }
}

TEST_CASE("PNG encoding is deterministic") {
  const auto first = encodePngRgba(sampleImage());
  const auto second = encodePngRgba(sampleImage());
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  CHECK(*first == *second);
  CHECK_FALSE(hasChunk(*first, "tIME"));
  CHECK_FALSE(hasChunk(*first, "tEXt"));

  const std::vector<std::uint8_t> indices = {0, 1, 1, 0};
  const std::vector<Rgba8> palette = {{.r = 0, .g = 0, .b = 0, .a = 0},
                                      {.r = 1, .g = 2, .b = 3, .a = 255}};
  CHECK(*encodePngIndexed(2, 2, indices, palette) == *encodePngIndexed(2, 2, indices, palette));
}

TEST_CASE("an indexed PNG keeps its indices and palette, with alpha in tRNS") {
  const std::vector<std::uint8_t> indices = {0, 1, 2, 3, 3, 2};
  const std::vector<Rgba8> palette = {{.r = 0, .g = 0, .b = 0, .a = 0},
                                      {.r = 255, .g = 0, .b = 0, .a = 255},
                                      {.r = 0, .g = 255, .b = 0, .a = 255},
                                      {.r = 0, .g = 0, .b = 255, .a = 255}};
  const auto png = encodePngIndexed(3, 2, indices, palette);
  REQUIRE(png.has_value());
  CHECK(hasChunk(*png, "PLTE"));
  CHECK(hasChunk(*png, "tRNS"));

  // Decode without colour conversion to get the stored indices and palette back.
  lodepng::State state;
  state.decoder.color_convert = 0;
  std::vector<unsigned char> decoded;
  unsigned width = 0;
  unsigned height = 0;
  REQUIRE(lodepng::decode(decoded, width, height, state, toUnsignedChars(*png)) == 0);
  CHECK(state.info_png.color.colortype == LCT_PALETTE);
  CHECK(state.info_png.color.bitdepth == 8);
  CHECK(width == 3);
  CHECK(height == 2);
  CHECK(decoded == std::vector<unsigned char>(indices.begin(), indices.end()));
  REQUIRE(state.info_png.color.palettesize == palette.size());
  const std::span<const unsigned char> stored{state.info_png.color.palette, palette.size() * 4};
  for (std::size_t i = 0; i < palette.size(); ++i) {
    const Rgba8 color{.r = stored[i * 4],
                      .g = stored[(i * 4) + 1],
                      .b = stored[(i * 4) + 2],
                      .a = stored[(i * 4) + 3]};
    CHECK(color == palette[i]);
  }
}

TEST_CASE("an opaque palette needs no tRNS") {
  const std::vector<std::uint8_t> indices = {0, 1};
  const std::vector<Rgba8> palette = {{.r = 1, .g = 1, .b = 1, .a = 255},
                                      {.r = 2, .g = 2, .b = 2, .a = 255}};
  const auto png = encodePngIndexed(2, 1, indices, palette);
  REQUIRE(png.has_value());
  CHECK_FALSE(hasChunk(*png, "tRNS"));
}

TEST_CASE("PNG encoding rejects inconsistent input") {
  CHECK_FALSE(encodePngRgba(RgbaImage{.width = 0, .height = 0, .pixels = {}}).has_value());
  CHECK_FALSE(encodePngRgba(RgbaImage{.width = 2, .height = 2, .pixels = std::vector<Rgba8>(3)})
                  .has_value());

  const std::vector<std::uint8_t> indices = {0, 1, 2, 3};
  const std::vector<Rgba8> three(3);
  CHECK_FALSE(encodePngIndexed(2, 2, indices, three).has_value());
  CHECK_FALSE(encodePngIndexed(2, 2, indices, {}).has_value());
  CHECK_FALSE(encodePngIndexed(2, 2, indices, std::vector<Rgba8>(257)).has_value());
  CHECK_FALSE(encodePngIndexed(4, 2, indices, std::vector<Rgba8>(4)).has_value());
}
