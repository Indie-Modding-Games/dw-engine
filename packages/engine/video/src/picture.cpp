#include <dwe/video/picture.hpp>

#include <algorithm>
#include <cmath>

namespace dwe::video {

namespace {

// Full-range BT.601 (ITU-T T.871, the JPEG form): the coefficients the MDEC's fixed-point
// conversion rounds (1.402, -0.344, -0.714, 1.772).
constexpr float RedFromCr = 1.402F;
constexpr float GreenFromCb = -0.344136F;
constexpr float GreenFromCr = -0.714136F;
constexpr float BlueFromCb = 1.772F;

std::byte channel(float value) {
  return static_cast<std::byte>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

// The chroma plane's samples on each side of a pixel along one axis, and the weight of the second
// in quarters: a centred sample lies half a chroma sample (a quarter of the pair) off the pixel.
struct Tap {
  std::size_t near;
  std::size_t far;
};

Tap tapOf(std::size_t pixel, std::size_t chromaCount) {
  const std::size_t near = pixel / 2;
  // Even pixels lean to the sample before theirs, odd ones to the one after.
  const std::size_t far =
      pixel % 2 == 0 ? (near == 0 ? 0 : near - 1) : std::min(near + 1, chromaCount - 1);
  return {.near = near, .far = far};
}

} // namespace

std::size_t YuvPicture::chromaSize(std::uint32_t width, std::uint32_t height) {
  return std::size_t{(width + 1) / 2} * std::size_t{(height + 1) / 2};
}

void toRgba(const YuvPicture& picture, std::vector<std::byte>& rgba) {
  const std::size_t width = picture.width;
  const std::size_t height = picture.height;
  const std::size_t chromaWidth = (width + 1) / 2;
  const std::size_t chromaHeight = (height + 1) / 2;
  rgba.resize(width * height * 4);
  if (width == 0 || height == 0) {
    return;
  }
  // Codes to 8-bit levels: full range spans 0..2^n - 1 and grey is 2^(n - 1) (H.273).
  const float maxCode = static_cast<float>((1U << picture.bitDepth) - 1U);
  const float toLevel = 255.0F / maxCode;
  const float grey = static_cast<float>(1U << (picture.bitDepth - 1U));
  const auto chromaAt = [&](const std::vector<std::uint16_t>& plane, Tap row, Tap column) {
    const auto at = [&](std::size_t r, std::size_t c) {
      return static_cast<float>(plane[(r * chromaWidth) + c]);
    };
    // 9/16, 3/16, 3/16 and 1/16 from the nearest sample out.
    const float sum = (9.0F * at(row.near, column.near)) + (3.0F * at(row.near, column.far)) +
                      (3.0F * at(row.far, column.near)) + at(row.far, column.far);
    return ((sum / 16.0F) - grey) * toLevel;
  };
  for (std::size_t y = 0; y < height; ++y) {
    const Tap row = tapOf(y, chromaHeight);
    for (std::size_t x = 0; x < width; ++x) {
      const Tap column = tapOf(x, chromaWidth);
      const float luma = static_cast<float>(picture.luma[(y * width) + x]) * toLevel;
      const float blue = chromaAt(picture.blue, row, column);
      const float red = chromaAt(picture.red, row, column);
      const std::size_t at = ((y * width) + x) * 4;
      rgba[at] = channel(luma + (RedFromCr * red));
      rgba[at + 1] = channel(luma + (GreenFromCb * blue) + (GreenFromCr * red));
      rgba[at + 2] = channel(luma + (BlueFromCb * blue));
      rgba[at + 3] = std::byte{0xFF};
    }
  }
}

} // namespace dwe::video
