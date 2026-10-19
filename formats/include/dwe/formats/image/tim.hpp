#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dwe::formats::image {

// The first word of every TIM.
inline constexpr std::uint32_t TimMagic = 0x10;

// How a TIM stores its pixels: bits 0-2 of its flags word.
enum class TimPixelMode : std::uint8_t { Indexed4 = 0, Indexed8 = 1, Direct16 = 2, Direct24 = 3 };

// A rectangle of the console's video memory. x and width count 16-bit units, not pixels.
struct VramRect {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;

    bool operator==(const VramRect&) const = default;
};

// A colour as the console stores it: red in bits 0-4, green in 5-9, blue in 10-14 and the semi-transparency (STP)
// bit in 15.
using Color15 = std::uint16_t;

// A colour with 8 bits per channel.
struct Rgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;

    bool operator==(const Rgba8&) const = default;
};

// An image of 8-bit RGBA pixels, row by row from the top left.
struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<Rgba8> pixels;
};

// A TIM image as it is stored, so nothing is lost: the raw CLUT colours and the raw pixel words.
struct Tim {
    TimPixelMode mode = TimPixelMode::Direct16;
    bool hasClut = false;
    VramRect clutRect;           // where the original uploads the CLUT; width is colours per row
    std::vector<Color15> clut;   // clutRect.width × clutRect.height colours, row by row
    VramRect imageRect;          // where the original uploads the pixels
    std::vector<std::byte> data; // imageRect.width × 2 × imageRect.height bytes, row by row
    std::size_t byteSize = 0;    // the bytes the TIM takes according to its block lengths
};

// Reads the TIM at the start of the bytes. Bytes after its last block are ignored.
core::Result<Tim> readTim(std::span<const std::byte> bytes);

// Returns the width of the image in pixels.
std::uint32_t timWidth(const Tim& tim);

// Returns how many palettes the CLUT holds: each palette has 16 colours (4-bit) or 256 (8-bit), or the whole CLUT row
// when the row is shorter. Palettes are numbered left to right, then top to bottom. 0 for 16- and 24-bit images.
std::size_t timPaletteCount(const Tim& tim);

// Returns the raw colours of one palette of a 4- or 8-bit TIM.
core::Result<std::vector<Color15>> timPalette(const Tim& tim, std::size_t paletteIndex);

// Returns the colour index of every pixel of a 4- or 8-bit TIM, one byte per pixel, row by row.
core::Result<std::vector<std::uint8_t>> timIndices(const Tim& tim);

// Converts a console colour to RGBA8. Channels are widened by repeating their high bits (31 becomes 255). 0x0000 is
// the only transparent colour, because the console skips those texels; the STP bit only matters when a primitive is
// drawn semi-transparent, so it does not change alpha here (0x8000 is opaque black) and stays in the raw colours.
Rgba8 toRgba8(Color15 color);

// Decodes a TIM to RGBA8. Indexed images use the palette `paletteIndex`; 24-bit pixels are always opaque.
core::Result<RgbaImage> decodeTim(const Tim& tim, std::size_t paletteIndex = 0);

} // namespace dwe::formats::image
