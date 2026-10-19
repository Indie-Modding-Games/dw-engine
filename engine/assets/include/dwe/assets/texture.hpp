#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dwe::assets {

// How the texels of a loaded texture are stored.
enum class TexelEncoding : std::uint8_t {
    Rgba8, // 4 bytes per texel, row by row from the top left
    Bc7,   // 16 bytes per block of 4x4 texels, row by row from the top left; edge blocks are whole
};

// A texture ready to upload to the GPU.
struct TextureData {
    TexelEncoding encoding = TexelEncoding::Rgba8;
    bool srgb = false; // colour stored as sRGB; otherwise data, stored linear
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Largest first: level i measures max(width >> i, 1) by max(height >> i, 1).
    std::vector<std::vector<std::byte>> levels;
};

// Reads a 2D KTX2 texture with Basis Universal data (UASTC, as dwx cook writes it) and transcodes every mip level to
// the given encoding. Safe to call from several threads.
core::Result<TextureData> decodeKtx2(std::span<const std::byte> ktx2, TexelEncoding encoding);

} // namespace dwe::assets
