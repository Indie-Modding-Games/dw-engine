#include <dwe/assets/file_system.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/indexed_image.hpp>

#include <doctest/doctest.h>
#include <lodepng.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>

using dwe::assets::applyPalette;
using dwe::assets::decodeIndexedPng;
using dwe::assets::PaletteColor;

namespace {

std::vector<std::byte> toBytes(const std::vector<unsigned char>& values) {
    std::vector<std::byte> bytes(values.size());
    std::ranges::transform(values, bytes.begin(), [](unsigned char value) { return std::byte{value}; });
    return bytes;
}

// Encodes packed palette indices at the given bit depth with a palette of `colors` entries: entry i is (i, 2i, 3i)
// and entry 0 is transparent.
std::vector<std::byte> encodePalettePng(const std::vector<unsigned char>& packed, unsigned width, unsigned height,
                                        unsigned bitDepth, unsigned colors) {
    lodepng::State state;
    for (unsigned entry = 0; entry < colors; ++entry) {
        const auto value = static_cast<unsigned char>(entry);
        const auto alpha = static_cast<unsigned char>(entry == 0 ? 0 : 255);
        REQUIRE(lodepng_palette_add(&state.info_png.color, value, value * 2U, value * 3U, alpha) == 0);
        REQUIRE(lodepng_palette_add(&state.info_raw, value, value * 2U, value * 3U, alpha) == 0);
    }
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = bitDepth;
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = bitDepth;
    state.encoder.auto_convert = 0;
    std::vector<unsigned char> png;
    REQUIRE(lodepng::encode(png, packed, width, height, state) == 0);
    return toBytes(png);
}

} // namespace

TEST_CASE("an 8-bit palette PNG gives its indices and its palette") {
    const std::vector<unsigned char> indices{0, 1, 2, 3, 200, 7};
    const auto image = decodeIndexedPng(encodePalettePng(indices, 3, 2, 8, 256));
    REQUIRE(image.has_value());
    CHECK(image.value().width == 3);
    CHECK(image.value().height == 2);
    CHECK(image.value().indices == std::vector<std::uint8_t>{0, 1, 2, 3, 200, 7});
    REQUIRE(image.value().palette.size() == 256);
    CHECK(image.value().palette[0] == PaletteColor{.red = 0, .green = 0, .blue = 0, .alpha = 0});
    CHECK(image.value().palette[5] == PaletteColor{.red = 5, .green = 10, .blue = 15, .alpha = 255});
}

TEST_CASE("a 4-bit palette PNG is unpacked to one index per byte") {
    // Three pixels per row, packed without row padding as lodepng takes raw pixels; the file pads each row.
    const std::vector<unsigned char> packed{0x12, 0x3F, 0x00};
    const auto image = decodeIndexedPng(encodePalettePng(packed, 3, 2, 4, 16));
    REQUIRE(image.has_value());
    CHECK(image.value().indices == std::vector<std::uint8_t>{1, 2, 3, 15, 0, 0});
    CHECK(image.value().palette.size() == 16);
}

TEST_CASE("a PNG without a palette is refused") {
    std::vector<unsigned char> png;
    REQUIRE(lodepng::encode(png, std::vector<unsigned char>(16, 255), 2, 2, LCT_RGBA, 8) == 0);
    CHECK_FALSE(decodeIndexedPng(toBytes(png)).has_value());
    CHECK_FALSE(decodeIndexedPng(toBytes({1, 2, 3})).has_value());
}

TEST_CASE("indices become colours of any palette") {
    const dwe::assets::IndexedImage image{.width = 2, .height = 1, .indices = {1, 9}, .palette = {}};
    const std::vector<PaletteColor> palette{{.red = 1, .green = 2, .blue = 3, .alpha = 4},
                                            {.red = 10, .green = 20, .blue = 30, .alpha = 255}};
    const auto rgba = applyPalette(image, palette);
    CHECK(rgba.width == 2);
    CHECK(rgba.height == 1);
    const std::vector<std::byte> expected{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{255},
                                          std::byte{0},  std::byte{0},  std::byte{0},  std::byte{0}};
    CHECK(rgba.pixels == expected);
}

TEST_CASE("the user's disc, extracted: a map background decodes to indices that give the RGBA decoding back") {
    const char* content = std::getenv("DWE_CONTENT");
    if (content == nullptr || std::string_view{content}.empty()) {
        MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
        return;
    }
    const dwe::assets::DiskFileSystem disk;
    for (const char* name : {"background_day.png", "texture8/000.png", "texture4/000.png"}) {
        CAPTURE(name);
        const auto bytes = disk.readFile(std::filesystem::path{content} / "base/map/twna01" / name);
        REQUIRE(bytes.has_value());
        const auto indexed = decodeIndexedPng(bytes.value());
        REQUIRE(indexed.has_value());
        const auto rgba = dwe::assets::decodePng(bytes.value());
        REQUIRE(rgba.has_value());
        CHECK(applyPalette(indexed.value(), indexed.value().palette).pixels == rgba.value().pixels);
    }
}
