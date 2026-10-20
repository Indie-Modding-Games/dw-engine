#include <dwe/assets/image.hpp>

#include <doctest/doctest.h>
#include <lodepng.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using dwe::assets::decodePng;
using dwe::assets::encodePng;
using dwe::assets::Image;
using dwe::assets::inspectPng;
using dwe::assets::MaxImageSide;

namespace {

// PNG files keep the width of the image as a big-endian number right after the signature and the IHDR chunk header.
constexpr std::size_t WidthOffset = 16;
constexpr std::size_t IhdrOffset = 8;

std::vector<std::byte> toBytes(const std::vector<unsigned char>& values) {
    std::vector<std::byte> bytes(values.size());
    std::ranges::transform(values, bytes.begin(), [](unsigned char value) { return std::byte{value}; });
    return bytes;
}

std::vector<unsigned char> encode(const std::vector<unsigned char>& raw, unsigned width, unsigned height,
                                  LodePNGColorType colorType) {
    std::vector<unsigned char> png;
    REQUIRE(lodepng::encode(png, raw, width, height, colorType, 8) == 0);
    return png;
}

} // namespace

TEST_CASE("an RGBA PNG decodes to the same pixels") {
    const std::vector<unsigned char> raw{
        255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120,
    };
    const auto image = decodePng(toBytes(encode(raw, 3, 2, LCT_RGBA)));
    REQUIRE(image.has_value());
    CHECK(image->width == 3);
    CHECK(image->height == 2);
    CHECK(image->pixels == toBytes(raw));
}

TEST_CASE("a grey PNG decodes to opaque RGBA") {
    const auto image = decodePng(toBytes(encode({0, 200}, 2, 1, LCT_GREY)));
    REQUIRE(image.has_value());
    CHECK(image->pixels == toBytes({0, 0, 0, 255, 200, 200, 200, 255}));
}

TEST_CASE("a palettized PNG decodes to its colours, transparency included") {
    lodepng::State state;
    for (LodePNGColorMode* mode : {&state.info_raw, &state.info_png.color}) {
        mode->colortype = LCT_PALETTE;
        mode->bitdepth = 8;
        REQUIRE(lodepng_palette_add(mode, 0, 0, 0, 0) == 0);
        REQUIRE(lodepng_palette_add(mode, 255, 128, 0, 255) == 0);
    }
    state.encoder.auto_convert = 0;
    std::vector<unsigned char> png;
    REQUIRE(lodepng::encode(png, std::vector<unsigned char>{1, 0, 1}, 3, 1, state) == 0);

    const auto image = decodePng(toBytes(png));
    REQUIRE(image.has_value());
    CHECK(image->pixels == toBytes({255, 128, 0, 255, 0, 0, 0, 0, 255, 128, 0, 255}));
}

TEST_CASE("a PNG's header says its size and whether it is indexed") {
    lodepng::State state;
    for (LodePNGColorMode* mode : {&state.info_raw, &state.info_png.color}) {
        mode->colortype = LCT_PALETTE;
        mode->bitdepth = 8;
        REQUIRE(lodepng_palette_add(mode, 0, 0, 0, 255) == 0);
        REQUIRE(lodepng_palette_add(mode, 255, 255, 255, 255) == 0);
    }
    state.encoder.auto_convert = 0;
    std::vector<unsigned char> indexed;
    REQUIRE(lodepng::encode(indexed, std::vector<unsigned char>{0, 1, 1, 0, 1, 0}, 2, 3, state) == 0);

    const auto palette = inspectPng(toBytes(indexed));
    REQUIRE(palette.has_value());
    CHECK(palette->width == 2);
    CHECK(palette->height == 3);
    CHECK(palette->indexed);

    const auto rgba = inspectPng(toBytes(encode({1, 2, 3, 4, 5, 6, 7, 8}, 1, 2, LCT_RGBA)));
    REQUIRE(rgba.has_value());
    CHECK(rgba->width == 1);
    CHECK(rgba->height == 2);
    CHECK_FALSE(rgba->indexed);

    CHECK_FALSE(inspectPng(toBytes({'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'})).has_value());
}

TEST_CASE("bytes that are not a whole PNG are an error") {
    const std::vector<unsigned char> png = encode({1, 2, 3, 4}, 1, 1, LCT_RGBA);
    const std::vector<unsigned char> truncated(png.begin(), png.end() - 20);

    CHECK_FALSE(decodePng({}).has_value());
    CHECK_FALSE(decodePng(toBytes({'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'})).has_value());
    CHECK_FALSE(decodePng(toBytes(truncated)).has_value());
}

TEST_CASE("a PNG wider than the limit is an error before it is decoded") {
    std::vector<unsigned char> png = encode({1, 2, 3, 4}, 1, 1, LCT_RGBA);
    const std::uint32_t width = MaxImageSide + 1;
    png[WidthOffset] = static_cast<unsigned char>(width >> 24U);
    png[WidthOffset + 1] = static_cast<unsigned char>(width >> 16U);
    png[WidthOffset + 2] = static_cast<unsigned char>(width >> 8U);
    png[WidthOffset + 3] = static_cast<unsigned char>(width);
    lodepng_chunk_generate_crc(&png[IhdrOffset]);

    const auto image = decodePng(toBytes(png));
    REQUIRE_FALSE(image.has_value());
    CHECK(image.error().message.find("larger than") != std::string::npos);
}

TEST_CASE("an encoded image decodes to the same pixels") {
    const Image image{.width = 2,
                      .height = 1,
                      .pixels = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{255}, std::byte{4}, std::byte{5},
                                 std::byte{6}, std::byte{128}}};
    const auto png = encodePng(image);
    REQUIRE(png.has_value());
    const auto decoded = decodePng(*png);
    REQUIRE(decoded.has_value());
    CHECK(decoded->width == 2);
    CHECK(decoded->height == 1);
    CHECK(decoded->pixels == image.pixels);
}

TEST_CASE("an image whose pixels do not match its size is not encoded") {
    CHECK_FALSE(encodePng(Image{.width = 2, .height = 2, .pixels = std::vector<std::byte>(4)}).has_value());
    CHECK_FALSE(encodePng(Image{}).has_value());
}
