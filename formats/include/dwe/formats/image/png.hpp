#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/image/tim.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The PNG files the extractor writes. The same input always gives the same bytes: no time stamp, no text chunks and
// fixed encoder settings.
namespace dwe::formats::image {

// Encodes an image as an 8-bit RGBA PNG.
core::Result<std::vector<std::byte>> encodePngRgba(const RgbaImage& image);

// Encodes one colour index per pixel as an 8-bit palettized PNG: the palette goes in PLTE and, when some colour is
// not opaque, its alpha in tRNS. The palette has 1 to 256 colours and every index must be inside it.
core::Result<std::vector<std::byte>> encodePngIndexed(std::uint32_t width, std::uint32_t height,
                                                      std::span<const std::uint8_t> indices,
                                                      std::span<const Rgba8> palette);

} // namespace dwe::formats::image
