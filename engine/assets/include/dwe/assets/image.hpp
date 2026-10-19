#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dwe::assets {

// An image with 8-bit RGBA pixels, row by row from the top left: width * height * 4 bytes.
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> pixels;
};

// The longest side decodePng accepts: no GPU takes a larger texture, and a forged header cannot ask for gigabytes.
inline constexpr std::uint32_t MaxImageSide = 16384;

// Decodes a PNG of any colour type and bit depth to 8-bit RGBA.
core::Result<Image> decodePng(std::span<const std::byte> png);

} // namespace dwe::assets
