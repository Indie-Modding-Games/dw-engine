#include <dwe/assets/image_palettes.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using dwe::assets::imageColorFromOriginal;
using dwe::assets::PaletteColor;
using dwe::assets::parseImagePalettes;
using dwe::assets::parseOriginalPalette;

TEST_CASE("an image's metadata lists its palettes in the original's colours") {
    const auto palettes = parseImagePalettes("source: \"STDDAT/TIME.TIM\"\npalettes:\n- \"0000 7fff 8000\"\n"
                                             "- \"001f 03e0\"\n");
    REQUIRE(palettes.has_value());
    REQUIRE(palettes->size() == 2);
    CHECK((*palettes)[0] == std::vector<std::uint16_t>{0x0000, 0x7FFF, 0x8000});
    CHECK((*palettes)[1] == std::vector<std::uint16_t>{0x001F, 0x03E0});

    CHECK_FALSE(parseImagePalettes("source: x\n").has_value());
    CHECK_FALSE(parseImagePalettes("palettes:\n- \"00zz\"\n").has_value());
    CHECK_FALSE(parseImagePalettes("palettes:\n- [1, 2]\n").has_value());
    CHECK_FALSE(parseOriginalPalette("12345").has_value());
}

TEST_CASE("an image colour is transparent only for 0x0000, as the extracted PNGs hold it") {
    CHECK(imageColorFromOriginal(0x0000) == PaletteColor{});
    CHECK(imageColorFromOriginal(0x8000) == PaletteColor{.red = 0, .green = 0, .blue = 0, .alpha = 255});
    CHECK(imageColorFromOriginal(0x001F) == PaletteColor{.red = 255, .green = 0, .blue = 0, .alpha = 255});
    CHECK(imageColorFromOriginal(0xFC00) == PaletteColor{.red = 0, .green = 0, .blue = 255, .alpha = 255});
}
