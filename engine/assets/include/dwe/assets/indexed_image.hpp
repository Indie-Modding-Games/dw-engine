#pragma once

#include <dwe/assets/image.hpp>
#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dwe::assets {

// One colour of a palette, 8 bits per channel.
struct PaletteColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 0;

    friend bool operator==(const PaletteColor&, const PaletteColor&) = default;
};

// An image of palette indices, one byte per pixel row by row from the top left, and the palette it was saved with.
// The extracted map backgrounds and map textures keep their indices so the engine can draw them with another palette
// (the time of day, or a sprite's CLUT).
struct IndexedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> indices;
    std::vector<PaletteColor> palette;
};

// Decodes a palette PNG of 1, 2, 4 or 8 bits per pixel to one index per byte, with its PLTE and tRNS as the palette.
// Fails for PNGs without a palette.
core::Result<IndexedImage> decodeIndexedPng(std::span<const std::byte> png);

// Turns indices into RGBA with a palette; an index past the end of the palette gives transparent black.
Image applyPalette(const IndexedImage& image, std::span<const PaletteColor> palette);

} // namespace dwe::assets
