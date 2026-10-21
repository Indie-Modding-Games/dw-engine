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

// How the levels of a KTX2 texture are compressed on top of their texel format.
enum class Ktx2Supercompression : std::uint8_t {
    None,
    BasisLz, // ETC1S
    Zstd,
    Other, // zlib, or a scheme this engine does not know
};

// What the header and the level index of a KTX2 texture say, read without its texel data.
struct Ktx2Header {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0; // 1 for a 2D texture
    std::uint32_t dimensions = 0;
    std::uint32_t layers = 0;
    std::uint32_t faces = 0;
    std::uint32_t levels = 0;
    Ktx2Supercompression supercompression = Ktx2Supercompression::None;
    bool needsTranscoding = false; // the texels are Basis Universal data (UASTC or ETC1S)
    // What loading each level allocates, largest first: its size once inflated for Zstd, as stored otherwise. Every
    // level lies inside the file.
    std::vector<std::uint64_t> levelBytes;
};

// Reads the header and the level index of a KTX2 texture. Fails if the bytes do not start with valid ones.
core::Result<Ktx2Header> inspectKtx2(std::span<const std::byte> ktx2);

// Checks that a KTX2 texture is one decodeKtx2 reads, from its header alone: a single 2D image of 1 to maxSide
// pixels on a side, no more mip levels than its size has, Basis Universal data without supercompression or with
// BasisLZ or Zstd, and no level (but BasisLZ ones, which load as stored) that takes more bytes once loaded than a
// level of its size holds. Every KTX2 load runs it before loading any texel data.
core::Result<void> checkPlain2dKtx2(const Ktx2Header& header, std::uint32_t maxSide);

// Reads a 2D KTX2 texture with Basis Universal data (UASTC, as dwx cook writes it) and transcodes every mip level to
// the given encoding. The header is checked (checkPlain2dKtx2, up to MaxImageSide) before any texel data is loaded or
// inflated. Safe to call from several threads.
core::Result<TextureData> decodeKtx2(std::span<const std::byte> ktx2, TexelEncoding encoding);

} // namespace dwe::assets
