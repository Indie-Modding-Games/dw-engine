#pragma once

#include <dwe/assets/image.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dwe::dwx {

// The texel formats dwx writes to KTX2, by their VkFormat number.
enum class Ktx2Format : std::uint8_t {
    Rgba8Unorm = 37, // VK_FORMAT_R8G8B8A8_UNORM: data that is not colour
    Rgba8Srgb = 43,  // VK_FORMAT_R8G8B8A8_SRGB: colour
};

// Writes a 2D KTX 2.0 texture with uncompressed RGBA8 levels, given largest first. Each level must be half the size
// of the one before, rounding down and never below 1. The fields it fills are in docs/formats/ktx2.md.
std::vector<std::byte> writeKtx2(Ktx2Format format, std::span<const assets::Image> levels);

} // namespace dwe::dwx
