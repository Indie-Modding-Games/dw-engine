#include "texture_cooker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "ktx2.hpp"

namespace dwe::dwx {

namespace {

constexpr std::size_t Channels = 4;
constexpr std::size_t Alpha = 3;
constexpr float MaxByte = 255.0F;

// An image in linear light with colour premultiplied by alpha: what box filtering averages.
struct LinearImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> texels; // width * height * 4
};

// A source texel that a target texel covers, and the share of the target it makes up.
struct Tap {
    std::uint32_t source = 0;
    float weight = 0.0F;
};

// The taps of each target texel along one axis. In units of 1 / (source * target) of the axis, target texel i spans
// [i * source, (i + 1) * source) and source texel j spans [j * target, (j + 1) * target), so the overlaps are exact.
std::vector<std::vector<Tap>> axisTaps(std::uint32_t source, std::uint32_t target) {
    std::vector<std::vector<Tap>> taps(target);
    for (std::uint32_t i = 0; i < target; ++i) {
        const std::uint64_t begin = std::uint64_t{i} * source;
        const std::uint64_t end = begin + source;
        for (std::uint64_t j = begin / target; j * target < end; ++j) {
            const std::uint64_t overlap = std::min(end, (j + 1) * target) - std::max(begin, j * target);
            taps[i].push_back(Tap{.source = static_cast<std::uint32_t>(j),
                                  .weight = static_cast<float>(overlap) / static_cast<float>(source)});
        }
    }
    return taps;
}

float srgbToLinear(float value) {
    return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

float linearToSrgb(float value) {
    return value <= 0.0031308F ? value * 12.92F : (1.055F * std::pow(value, 1.0F / 2.4F)) - 0.055F;
}

std::byte toByte(float value) {
    return static_cast<std::byte>(std::lround(std::clamp(value, 0.0F, 1.0F) * MaxByte));
}

LinearImage toLinear(const assets::Image& image, bool srgb) {
    std::array<float, 256> decode{};
    for (std::size_t i = 0; i < decode.size(); ++i) {
        const float value = static_cast<float>(i) / MaxByte;
        decode[i] = srgb ? srgbToLinear(value) : value;
    }
    LinearImage linear{.width = image.width, .height = image.height, .texels = std::vector<float>(image.pixels.size())};
    for (std::size_t texel = 0; texel < image.pixels.size(); texel += Channels) {
        const float alpha = static_cast<float>(std::to_integer<std::uint8_t>(image.pixels[texel + Alpha])) / MaxByte;
        for (std::size_t channel = 0; channel < Alpha; ++channel) {
            linear.texels[texel + channel] =
                decode[std::to_integer<std::size_t>(image.pixels[texel + channel])] * alpha;
        }
        linear.texels[texel + Alpha] = alpha;
    }
    return linear;
}

assets::Image toImage(const LinearImage& linear, bool srgb) {
    assets::Image image{
        .width = linear.width, .height = linear.height, .pixels = std::vector<std::byte>(linear.texels.size())};
    for (std::size_t texel = 0; texel < linear.texels.size(); texel += Channels) {
        const float alpha = linear.texels[texel + Alpha];
        for (std::size_t channel = 0; channel < Alpha; ++channel) {
            const float value = alpha > 0.0F ? linear.texels[texel + channel] / alpha : 0.0F;
            image.pixels[texel + channel] = toByte(srgb ? linearToSrgb(value) : value);
        }
        image.pixels[texel + Alpha] = toByte(alpha);
    }
    return image;
}

// Halves an image with a separable box filter: first along x, then along y.
LinearImage halve(const LinearImage& source) {
    const std::uint32_t width = std::max(source.width / 2, 1U);
    const std::uint32_t height = std::max(source.height / 2, 1U);
    const auto columns = axisTaps(source.width, width);
    const auto rows = axisTaps(source.height, height);
    std::vector<float> narrow(std::size_t{width} * source.height * Channels);
    for (std::size_t y = 0; y < source.height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            for (const Tap& tap : columns[x]) {
                for (std::size_t channel = 0; channel < Channels; ++channel) {
                    narrow[(((y * width) + x) * Channels) + channel] +=
                        source.texels[(((y * source.width) + tap.source) * Channels) + channel] * tap.weight;
                }
            }
        }
    }
    LinearImage halved{
        .width = width, .height = height, .texels = std::vector<float>(std::size_t{width} * height * Channels)};
    for (std::size_t y = 0; y < height; ++y) {
        for (const Tap& tap : rows[y]) {
            for (std::size_t x = 0; x < width; ++x) {
                for (std::size_t channel = 0; channel < Channels; ++channel) {
                    halved.texels[(((y * width) + x) * Channels) + channel] +=
                        narrow[(((std::size_t{tap.source} * width) + x) * Channels) + channel] * tap.weight;
                }
            }
        }
    }
    return halved;
}

} // namespace

std::vector<assets::Image> buildMipChain(const assets::Image& image, bool srgb) {
    std::vector<assets::Image> levels{image};
    LinearImage linear = toLinear(image, srgb);
    while (linear.width > 1 || linear.height > 1) {
        linear = halve(linear);
        levels.push_back(toImage(linear, srgb));
    }
    return levels;
}

bool isColorTexture(const assets::AssetId& id) {
    const std::string_view text = id.str();
    const std::string_view name = text.substr(text.rfind('/') + 1);
    return !name.ends_with("_depth") && !name.ends_with("_mask") && !name.ends_with("_normal");
}

core::Result<std::vector<std::byte>> cookTexture(std::span<const std::byte> png, const assets::AssetId& id) {
    const auto image = assets::decodePng(png);
    if (!image) {
        return std::unexpected{image.error()};
    }
    const bool srgb = isColorTexture(id);
    return writeKtx2(srgb ? Ktx2Format::Rgba8Srgb : Ktx2Format::Rgba8Unorm, buildMipChain(*image, srgb));
}

} // namespace dwe::dwx
