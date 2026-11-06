#include <dwe/assets/indexed_image.hpp>

#include <lodepng.h>

#include <algorithm>
#include <array>
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
  // Every index's four bytes, made once: an index past the palette is transparent black. The loop
  // writes through plain pointers, as the UI decodes whole sprite sheets with it and a checked
  // access per byte made that slow in debug builds.
  constexpr std::size_t Channels = 4;
  constexpr std::size_t Indices = 256;
  std::array<std::byte, Indices * Channels> colors{};
  for (std::size_t index = 0; index < std::min(palette.size(), Indices); ++index) {
    const PaletteColor& color = palette[index];
    colors.at(index * Channels) = std::byte{color.red};
    colors.at((index * Channels) + 1) = std::byte{color.green};
    colors.at((index * Channels) + 2) = std::byte{color.blue};
    colors.at((index * Channels) + 3) = std::byte{color.alpha};
  }
  const std::byte* const table = colors.data();
  Image result{.width = image.width, .height = image.height, .pixels = {}};
  result.pixels.resize(image.indices.size() * Channels);
  std::byte* out = result.pixels.data();
  const std::uint8_t* in = image.indices.data();
  const std::uint8_t* const end = in + image.indices.size();
  for (; in != end; ++in, out += Channels) {
    const std::byte* color = table + (std::size_t{*in} * Channels);
    out[0] = color[0];
    out[1] = color[1];
    out[2] = color[2];
    out[3] = color[3];
  }
  return result;
}

} // namespace dwe::assets
