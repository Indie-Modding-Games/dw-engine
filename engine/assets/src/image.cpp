#include <dwe/assets/image.hpp>

#include <lodepng.h>

#include <algorithm>
#include <format>
#include <span>

namespace dwe::assets {

core::Result<PngHeader> inspectPng(std::span<const std::byte> png) {
  unsigned width = 0;
  unsigned height = 0;
  lodepng::State state;
  if (const unsigned error = lodepng_inspect(
          &width, &height, &state, reinterpret_cast<const unsigned char*>(png.data()), png.size());
      error != 0) {
    return core::fail(std::format("not a PNG: {}", lodepng_error_text(error)));
  }
  return PngHeader{
      .width = width, .height = height, .indexed = state.info_png.color.colortype == LCT_PALETTE};
}

core::Result<Image> decodePng(std::span<const std::byte> png) {
  const auto* data = reinterpret_cast<const unsigned char*>(png.data());
  unsigned width = 0;
  unsigned height = 0;
  lodepng::State state;
  if (const unsigned error = lodepng_inspect(&width, &height, &state, data, png.size());
      error != 0) {
    return core::fail(std::format("not a PNG: {}", lodepng_error_text(error)));
  }
  if (width > MaxImageSide || height > MaxImageSide) {
    return core::fail(
        std::format("the PNG is {}x{}, larger than {} on a side", width, height, MaxImageSide));
  }
  std::vector<unsigned char> rgba;
  if (const unsigned error = lodepng::decode(rgba, width, height, data, png.size(), LCT_RGBA, 8);
      error != 0) {
    return core::fail(std::format("cannot decode the PNG: {}", lodepng_error_text(error)));
  }
  Image image{.width = width, .height = height, .pixels = std::vector<std::byte>(rgba.size())};
  // A copy of bytes to bytes is one memmove, even in debug builds, where a loop per byte is slow.
  std::ranges::copy(std::as_bytes(std::span{rgba}), image.pixels.begin());
  return image;
}

core::Result<std::vector<std::byte>> encodePng(const Image& image) {
  if (image.width == 0 || image.height == 0 ||
      image.pixels.size() != std::size_t{image.width} * image.height * 4) {
    return core::fail(std::format("cannot encode a {}x{} image from {} bytes", image.width,
                                  image.height, image.pixels.size()));
  }
  std::vector<unsigned char> png;
  const auto* pixels = reinterpret_cast<const unsigned char*>(image.pixels.data());
  if (const unsigned error = lodepng::encode(png, pixels, image.width, image.height, LCT_RGBA, 8);
      error != 0) {
    return core::fail(std::format("cannot encode the PNG: {}", lodepng_error_text(error)));
  }
  std::vector<std::byte> bytes(png.size());
  std::ranges::copy(std::as_bytes(std::span{png}), bytes.begin());
  return bytes;
}

} // namespace dwe::assets
