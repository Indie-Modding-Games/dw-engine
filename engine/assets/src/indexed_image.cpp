#include <dwe/assets/indexed_image.hpp>

#include <lodepng.h>

#include <format>

namespace dwe::assets {

core::Result<IndexedImage> decodeIndexedPng(std::span<const std::byte> png) {
  const auto* data = reinterpret_cast<const unsigned char*>(png.data());
  unsigned width = 0;
  unsigned height = 0;
  lodepng::State state;
  if (const unsigned error = lodepng_inspect(&width, &height, &state, data, png.size());
      error != 0) {
    return core::fail(std::format("not a PNG: {}", lodepng_error_text(error)));
  }
  if (state.info_png.color.colortype != LCT_PALETTE) {
    return core::fail("the PNG has no palette");
  }
  if (width > MaxImageSide || height > MaxImageSide) {
    return core::fail(
        std::format("the PNG is {}x{}, larger than {} on a side", width, height, MaxImageSide));
  }
  // Asking for 8-bit palette output with an empty output palette makes lodepng unpack the indices
  // as they are.
  state.info_raw.colortype = LCT_PALETTE;
  state.info_raw.bitdepth = 8;
  std::vector<unsigned char> indices;
  if (const unsigned error = lodepng::decode(indices, width, height, state, data, png.size());
      error != 0) {
    return core::fail(std::format("cannot decode the PNG: {}", lodepng_error_text(error)));
  }
  IndexedImage image{
      .width = width, .height = height, .indices = std::move(indices), .palette = {}};
  const LodePNGColorMode& color = state.info_png.color;
  image.palette.reserve(color.palettesize);
  for (std::size_t entry = 0; entry < color.palettesize; ++entry) {
    const unsigned char* rgba = color.palette + (entry * 4);
    image.palette.push_back({.red = rgba[0], .green = rgba[1], .blue = rgba[2], .alpha = rgba[3]});
  }
  return image;
}

Image applyPalette(const IndexedImage& image, std::span<const PaletteColor> palette) {
  Image result{.width = image.width, .height = image.height, .pixels = {}};
  result.pixels.resize(image.indices.size() * 4);
  std::size_t out = 0;
  for (const std::uint8_t index : image.indices) {
    const PaletteColor color = index < palette.size() ? palette[index] : PaletteColor{};
    result.pixels[out++] = std::byte{color.red};
    result.pixels[out++] = std::byte{color.green};
    result.pixels[out++] = std::byte{color.blue};
    result.pixels[out++] = std::byte{color.alpha};
  }
  return result;
}

} // namespace dwe::assets
