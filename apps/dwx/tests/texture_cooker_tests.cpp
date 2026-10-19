#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/image.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ktx2.hpp"
#include "texture_cooker.hpp"

using namespace dwe::dwx;
using dwe::assets::AssetId;
using dwe::assets::Image;

namespace {

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

std::uint64_t u64At(const std::vector<std::byte>& bytes, std::size_t offset) {
    return u32At(bytes, offset) | (std::uint64_t{u32At(bytes, offset + 4)} << 32U);
}

AssetId idOf(std::string_view text) {
    auto id = AssetId::parse(text);
    REQUIRE(id.has_value());
    return *id;
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

TEST_CASE("a KTX2 texture is laid out as the Khronos specification says") {
    const auto levels = buildMipChain(imageOf(4, 2, std::vector<std::uint8_t>(std::size_t{4} * 2 * 4, 7)), true);
    const auto bytes = writeKtx2(Ktx2Format::Rgba8Srgb, levels);
    const std::array<std::uint8_t, 12> identifier = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    for (std::size_t i = 0; i < identifier.size(); ++i) {
        CHECK(std::to_integer<std::uint8_t>(bytes[i]) == identifier[i]);
    }
    CHECK(u32At(bytes, 12) == 43); // VK_FORMAT_R8G8B8A8_SRGB
    CHECK(u32At(bytes, 16) == 1);  // typeSize
    CHECK(u32At(bytes, 20) == 4);
    CHECK(u32At(bytes, 24) == 2);
    CHECK(u32At(bytes, 28) == 0); // depth
    CHECK(u32At(bytes, 32) == 0); // layers
    CHECK(u32At(bytes, 36) == 1); // faces
    CHECK(u32At(bytes, 40) == 3); // levels
    CHECK(u32At(bytes, 44) == 0); // supercompression
    const std::uint32_t dfdOffset = u32At(bytes, 48);
    CHECK(dfdOffset == 80 + (3 * 24));
    CHECK(u32At(bytes, 52) == 92);
    CHECK(u32At(bytes, 56) == dfdOffset + 92);
    CHECK(u64At(bytes, 64) == 0); // no supercompression data
    CHECK(u64At(bytes, 72) == 0);

    // The DFD: total size, basic block of 88 bytes, RGBSDA / BT.709 / sRGB, 4 bytes per texel, linear alpha.
    CHECK(u32At(bytes, dfdOffset) == 92);
    CHECK(u32At(bytes, dfdOffset + 8) == (2U | (88U << 16U)));
    CHECK(u32At(bytes, dfdOffset + 12) == (1U | (1U << 8U) | (2U << 16U)));
    CHECK(u32At(bytes, dfdOffset + 20) == 4);
    CHECK(u32At(bytes, dfdOffset + 28) == (0U | (7U << 16U) | (0U << 24U)));
    CHECK(u32At(bytes, dfdOffset + 28 + (3 * 16)) == (24U | (7U << 16U) | (0x1FU << 24U)));
    CHECK(u32At(bytes, dfdOffset + 28 + 12) == 255);

    // Levels: smallest first in the file, each right after the next smaller one.
    const std::uint64_t level0 = u64At(bytes, 80);
    const std::uint64_t level1 = u64At(bytes, 80 + 24);
    const std::uint64_t level2 = u64At(bytes, 80 + 48);
    CHECK(u64At(bytes, 80 + 8) == 32);
    CHECK(u64At(bytes, 80 + 16) == 32);
    CHECK(level2 % 4 == 0);
    CHECK(level1 == level2 + 4);
    CHECK(level0 == level1 + 8);
    CHECK(bytes.size() == level0 + 32);
    CHECK(std::to_integer<std::uint8_t>(bytes[level0]) == 7);

    const std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    CHECK(text.contains(std::string_view{"KTXorientation\0rd\0", 18}));
    CHECK(text.contains(std::string_view{"KTXwriter\0dwx\0", 14}));

    const auto linear = writeKtx2(Ktx2Format::Rgba8Unorm, levels);
    CHECK(u32At(linear, 12) == 37); // VK_FORMAT_R8G8B8A8_UNORM
    CHECK(u32At(linear, dfdOffset + 12) == (1U | (1U << 8U) | (1U << 16U)));
    CHECK(u32At(linear, dfdOffset + 28 + (3 * 16)) == (24U | (7U << 16U) | (0x0FU << 24U)));
}

TEST_CASE("textures are colour unless their name says otherwise") {
    CHECK(isColorTexture(idOf("map/start/background_day")));
    CHECK(isColorTexture(idOf("masks/background")));
    CHECK_FALSE(isColorTexture(idOf("map/start/background_depth")));
    CHECK_FALSE(isColorTexture(idOf("digimon/x/skin_normal")));
    CHECK_FALSE(isColorTexture(idOf("ui_mask")));
}
