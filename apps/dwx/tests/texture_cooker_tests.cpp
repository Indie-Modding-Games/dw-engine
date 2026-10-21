#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/texture.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/image/png.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "texture_cooker.hpp"

using namespace dwe::dwx;
using dwe::assets::decodeKtx2;
using dwe::assets::Image;
using dwe::assets::TexelEncoding;
using dwe::core::YamlNode;
using dwe::formats::image::encodePngIndexed;
using dwe::formats::image::encodePngRgba;
using dwe::formats::image::Rgba8;
using dwe::formats::image::RgbaImage;

namespace {

dwe::assets::AssetId idOf(std::string_view text) {
    auto id = dwe::assets::AssetId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

// The id of an image the game loads as any texture.
const dwe::assets::AssetId& textureId() {
    static const dwe::assets::AssetId id = idOf("texture/card/001");
    return id;
}

Image imageOf(std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& rgba) {
    REQUIRE(rgba.size() == std::size_t{width} * height * 4);
    Image image{.width = width, .height = height, .pixels = {}};
    for (const std::uint8_t value : rgba) {
        image.pixels.push_back(std::byte{value});
    }
    return image;
}

std::vector<std::uint8_t> valuesOf(const Image& image) {
    std::vector<std::uint8_t> values;
    values.reserve(image.pixels.size());
    for (const std::byte value : image.pixels) {
        values.push_back(std::to_integer<std::uint8_t>(value));
    }
    return values;
}

std::uint32_t u32At(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
    }
    return value;
}

YamlNode yamlOf(std::string_view text) {
    auto node = dwe::core::parseYaml(text);
    REQUIRE(node.has_value());
    return std::move(*node);
}

} // namespace

TEST_CASE("a mip chain halves each side down to 1x1") {
    const Image image{.width = 5, .height = 3, .pixels = std::vector<std::byte>(std::size_t{5} * 3 * 4)};
    const auto levels = buildMipChain(image, true);
    REQUIRE(levels.size() == 3);
    CHECK(levels[1].width == 2);
    CHECK(levels[1].height == 1);
    CHECK(levels[2].width == 1);
    CHECK(levels[2].height == 1);
    CHECK(levels[0].pixels == image.pixels);
    CHECK(buildMipChain(imageOf(1, 1, {1, 2, 3, 4}), true).size() == 1);
}

TEST_CASE("mip levels average the area they cover") {
    // Linear data averages the values; three texels into one weigh a third each.
    CHECK(valuesOf(buildMipChain(imageOf(2, 1, {0, 100, 200, 255, 100, 200, 0, 255}), false)[1]) ==
          std::vector<std::uint8_t>{50, 150, 100, 255});
    CHECK(valuesOf(buildMipChain(imageOf(3, 1, {0, 0, 0, 255, 90, 0, 0, 255, 180, 0, 0, 255}), false)[1]) ==
          std::vector<std::uint8_t>{90, 0, 0, 255});
    // Colour averages in linear light: three white texels and a black one are lighter than 3/4 of 255.
    const std::vector<std::uint8_t> white = {255, 255, 255, 255};
    const std::vector<std::uint8_t> black = {0, 0, 0, 255};
    std::vector<std::uint8_t> square = white;
    square.insert(square.end(), white.begin(), white.end());
    square.insert(square.end(), white.begin(), white.end());
    square.insert(square.end(), black.begin(), black.end());
    CHECK(valuesOf(buildMipChain(imageOf(2, 2, square), true)[1]) == std::vector<std::uint8_t>{225, 225, 225, 255});
    CHECK(valuesOf(buildMipChain(imageOf(2, 2, square), false)[1]) == std::vector<std::uint8_t>{191, 191, 191, 255});
    // A transparent texel lowers the alpha but does not darken the colour.
    CHECK(valuesOf(buildMipChain(imageOf(2, 1, {255, 0, 0, 255, 0, 0, 0, 0}), true)[1]) ==
          std::vector<std::uint8_t>{255, 0, 0, 128});
}

TEST_CASE("a texture cooks to UASTC KTX2 that transcodes to BC7 and RGBA8") {
    // A 6x5 gradient with one transparent texel: no side is a multiple of 4, and the chain is 6x5, 3x2, 1x1. Like any
    // block format, UASTC fits a block's colours to a line, so the colour changes along one direction only.
    std::vector<Rgba8> pixels;
    for (std::uint8_t y = 0; y < 5; ++y) {
        for (std::uint8_t x = 0; x < 6; ++x) {
            pixels.push_back(Rgba8{.r = static_cast<std::uint8_t>(x * 40),
                                   .g = static_cast<std::uint8_t>(255 - (x * 40)),
                                   .b = 128,
                                   .a = static_cast<std::uint8_t>(x == 0 && y == 0 ? 0 : 255)});
        }
    }
    const auto png = encodePngRgba(RgbaImage{.width = 6, .height = 5, .pixels = pixels});
    REQUIRE(png.has_value());
    const auto cooked = cookTexture(*png, CookOptions{});
    REQUIRE(cooked.has_value());
    const std::array<std::uint8_t, 12> identifier = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    for (std::size_t i = 0; i < identifier.size(); ++i) {
        CHECK(std::to_integer<std::uint8_t>((*cooked)[i]) == identifier[i]);
    }
    CHECK(u32At(*cooked, 12) == 0); // VK_FORMAT_UNDEFINED: Basis Universal data
    CHECK(u32At(*cooked, 44) == 2); // supercompression scheme: zstd
    const std::string_view text{reinterpret_cast<const char*>(cooked->data()), cooked->size()};
    CHECK(text.contains(std::string_view{"KTXorientation\0rd\0", 18}));
    CHECK(text.contains("KTXwriter\0dwx cook / libktx "));

    const std::array<std::pair<std::uint32_t, std::uint32_t>, 3> sizes{{{6, 5}, {3, 2}, {1, 1}}};
    const auto bc7 = decodeKtx2(*cooked, TexelEncoding::Bc7);
    REQUIRE(bc7.has_value());
    CHECK(bc7->encoding == TexelEncoding::Bc7);
    CHECK(bc7->srgb);
    CHECK(bc7->width == 6);
    CHECK(bc7->height == 5);
    REQUIRE(bc7->levels.size() == sizes.size());
    CHECK(bc7->levels[0].size() == std::size_t{2} * 2 * 16); // 2x2 blocks of 16 bytes
    CHECK(bc7->levels[1].size() == 16);
    CHECK(bc7->levels[2].size() == 16);

    const auto rgba = decodeKtx2(*cooked, TexelEncoding::Rgba8);
    REQUIRE(rgba.has_value());
    CHECK(rgba->srgb);
    REQUIRE(rgba->levels.size() == sizes.size());
    for (std::size_t level = 0; level < sizes.size(); ++level) {
        CHECK(rgba->levels[level].size() == std::size_t{sizes[level].first} * sizes[level].second * 4);
    }
    // UASTC is nearly lossless: every channel of level 0 stays within a few steps of the source. The colour of a
    // transparent texel does not count.
    int worst = 0;
    for (std::size_t texel = 0; texel < pixels.size(); ++texel) {
        const std::array<std::uint8_t, 4> source{pixels[texel].r, pixels[texel].g, pixels[texel].b, pixels[texel].a};
        for (std::size_t channel = source[3] == 0 ? 3 : 0; channel < 4; ++channel) {
            const int decoded = std::to_integer<std::uint8_t>(rgba->levels[0][(texel * 4) + channel]);
            worst = std::max(worst, std::abs(decoded - int{source[channel]}));
        }
    }
    CHECK(worst <= 8);

    const auto linear = cookTexture(*png, CookOptions{.passThrough = false, .colorSpace = ColorSpace::Linear});
    REQUIRE(linear.has_value());
    const auto data = decodeKtx2(*linear, TexelEncoding::Rgba8);
    REQUIRE(data.has_value());
    CHECK_FALSE(data->srgb);
}

TEST_CASE("cooking a texture always gives the same bytes") {
    std::vector<Rgba8> pixels;
    pixels.reserve(std::size_t{37} * 23);
    for (std::uint32_t i = 0; i < 37U * 23U; ++i) {
        pixels.push_back(Rgba8{.r = static_cast<std::uint8_t>(i * 7),
                               .g = static_cast<std::uint8_t>(i * 13),
                               .b = static_cast<std::uint8_t>(i / 3),
                               .a = static_cast<std::uint8_t>(i % 5 == 0 ? 0 : 255)});
    }
    const auto png = encodePngRgba(RgbaImage{.width = 37, .height = 23, .pixels = pixels});
    REQUIRE(png.has_value());
    const auto first = cookTexture(*png, CookOptions{});
    REQUIRE(first.has_value());
    // Again, and on other threads at the same time, as dwx cook runs them.
    std::array<dwe::core::Result<std::vector<std::byte>>, 4> parallel;
    {
        std::vector<std::jthread> threads;
        threads.reserve(parallel.size());
        for (auto& result : parallel) {
            threads.emplace_back([&png, &result] { result = cookTexture(*png, CookOptions{}); });
        }
    }
    const auto again = cookTexture(*png, CookOptions{});
    REQUIRE(again.has_value());
    CHECK(*again == *first);
    for (const auto& result : parallel) {
        REQUIRE(result.has_value());
        CHECK(*result == *first);
    }
}

TEST_CASE("only indexed images whose palette is chosen at runtime pass through") {
    const std::array<std::uint8_t, 4> indices{0, 1, 1, 0};
    const std::array palette{Rgba8{.r = 0, .g = 0, .b = 0, .a = 255}, Rgba8{.r = 255, .g = 0, .b = 0, .a = 255}};
    const auto indexed = encodePngIndexed(2, 2, indices, palette);
    REQUIRE(indexed.has_value());
    const auto rgba = encodePngRgba(RgbaImage{.width = 1, .height = 1, .pixels = {palette[1]}});
    REQUIRE(rgba.has_value());

    // An indexed PNG with one palette, or with no sidecar, cooks.
    const auto bare = planTexture(textureId(), *indexed, nullptr);
    REQUIRE(bare.has_value());
    CHECK_FALSE(bare->passThrough);
    CHECK(bare->colorSpace == ColorSpace::Srgb);
    const YamlNode onePalette =
        yamlOf("source: \"CARD/CARD.ALL\"\nclut_rect: {x: 0, y: 480, width: 256, height: 1}\npng: indexed\n"
               "palettes:\n- \"0000 7fff\"\n");
    CHECK_FALSE(planTexture(textureId(), *indexed, &onePalette)->passThrough);

    // Several CLUT rows for the same pixels: the game picks one, so the indices stay.
    const YamlNode rows = yamlOf("source: \"CHDAT/ALLTIM.TIM\"\nclut_rect: {x: 0, y: 481, width: 16, height: 3}\n"
                                 "palettes:\n- \"0000 7fff\"\n- \"0000 001f\"\n- \"0000 03e0\"\n");
    CHECK(planTexture(textureId(), *indexed, &rows)->passThrough);
    // A map TIM: its sprites pick the row, even when its CLUT has one.
    const YamlNode map = yamlOf("source: \"MAP/MAP1/MAYO06.MAP\"\nclut_rect: {x: 0, y: 480, width: 256, height: 1}\n"
                                "palettes:\n- \"0000 7fff\"\n");
    CHECK(planTexture(textureId(), *indexed, &map)->passThrough);
    const YamlNode directMap = yamlOf("source: \"MAP/MAP1/MAYO06.MAP\"\nmode: direct16\n");
    CHECK_FALSE(planTexture(textureId(), *indexed, &directMap)->passThrough);

    // An RGBA PNG has no palette left to choose.
    CHECK_FALSE(planTexture(textureId(), *rgba, nullptr)->passThrough);
    CHECK_FALSE(planTexture(textureId(), *rgba, &rows)->passThrough);
    CHECK_FALSE(planTexture(textureId(), *rgba, &map)->passThrough);

    CHECK_FALSE(planTexture(textureId(), std::vector<std::byte>(8), nullptr).has_value());
}

TEST_CASE("images the game reads from their PNG pass through whatever their sidecar says") {
    const std::array<std::uint8_t, 4> indices{0, 1, 1, 0};
    const std::array palette{Rgba8{.r = 0, .g = 0, .b = 0, .a = 0}, Rgba8{.r = 255, .g = 255, .b = 255, .a = 255}};
    const auto indexed = encodePngIndexed(2, 2, indices, palette);
    REQUIRE(indexed.has_value());
    const YamlNode onePalette = yamlOf("png: indexed\npalettes:\n- \"0000 7fff\"\ncolor_space: linear\n");

    // A map background has no sidecar; a texture page's sidecar may come from a mod and name no .MAP; a font atlas's
    // sidecar describes its glyphs.
    for (const std::string_view id : {"map/field/background_day", "map/mod/town/background_night",
                                      "map/field/texture8/000", "map/mod/town/texture4/002", "font/main"}) {
        CAPTURE(id);
        for (const YamlNode* sidecar : {static_cast<const YamlNode*>(nullptr), &onePalette}) {
            const auto plan = planTexture(idOf(id), *indexed, sidecar);
            REQUIRE(plan.has_value());
            CHECK(plan->passThrough);
        }
    }
    // Their metadata, and images of other folders, are not such images.
    CHECK_FALSE(planTexture(idOf("map/field/background_day_meta"), *indexed, nullptr)->passThrough);
    CHECK_FALSE(planTexture(idOf("map/field/sky"), *indexed, nullptr)->passThrough);
    // A file that is not a PNG still fails.
    CHECK_FALSE(planTexture(idOf("font/main"), std::vector<std::byte>(8), nullptr).has_value());
}

TEST_CASE("the colour space of a texture comes from its sidecar") {
    const auto rgba =
        encodePngRgba(RgbaImage{.width = 1, .height = 1, .pixels = {Rgba8{.r = 1, .g = 2, .b = 3, .a = 4}}});
    REQUIRE(rgba.has_value());
    const YamlNode linear = yamlOf("color_space: linear\n");
    const YamlNode srgb = yamlOf("color_space: srgb\n");
    CHECK(planTexture(textureId(), *rgba, &linear)->colorSpace == ColorSpace::Linear);
    CHECK(planTexture(textureId(), *rgba, &srgb)->colorSpace == ColorSpace::Srgb);

    // A name no longer decides it.
    CHECK(planTexture(textureId(), *rgba, nullptr)->colorSpace == ColorSpace::Srgb);

    const YamlNode unknown = yamlOf("color_space: gamma\n");
    const auto wrong = planTexture(textureId(), *rgba, &unknown);
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().message.contains("color_space"));
    const YamlNode list = yamlOf("- color_space\n");
    CHECK_FALSE(planTexture(textureId(), *rgba, &list).has_value());
}
