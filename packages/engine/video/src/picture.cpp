#include <dwe/video/picture.hpp>

#include <algorithm>

namespace dwe::video {

namespace {

// The decoder keeps 9 bits of each channel and saturates them to a signed byte, stored + 128.
int saturateChannel(int value) {
  const auto nineBits = static_cast<int>(static_cast<unsigned>(value) & 0x1FFU);
  return std::clamp(nineBits >= 0x100 ? nineBits - 0x200 : nineBits, -128, 127) + 128;
}

struct Rgb {
  std::byte r;
  std::byte g;
  std::byte b;
};

// The MDEC's YCbCr to RGB (as formats/video toRgb, checked against Mednafen's model): the green
// term drops low bits of each product, as the hardware does.
Rgb toRgb(std::uint8_t y, std::uint8_t cb, std::uint8_t cr) {
  const int luma = int{y} - 128;
  const int blue = int{cb} - 128;
  const int red = int{cr} - 128;
  const int green = (static_cast<int>(static_cast<unsigned>(-88 * blue) & ~0x1FU) +
                     static_cast<int>(static_cast<unsigned>(-183 * red) & ~0x07U) + 0x80) >>
                    8;
  return Rgb{.r = static_cast<std::byte>(saturateChannel(luma + (((359 * red) + 0x80) >> 8))),
             .g = static_cast<std::byte>(saturateChannel(luma + green)),
             .b = static_cast<std::byte>(saturateChannel(luma + (((454 * blue) + 0x80) >> 8)))};
}

} // namespace

std::size_t YuvPicture::chromaSize(std::uint32_t width, std::uint32_t height) {
  return std::size_t{(width + 1) / 2} * std::size_t{(height + 1) / 2};
}

void toRgba(const YuvPicture& picture, std::vector<std::byte>& rgba) {
  const std::size_t width = picture.width;
  const std::size_t chromaWidth = (width + 1) / 2;
  rgba.resize(width * picture.height * 4);
  for (std::size_t row = 0; row < picture.height; ++row) {
    for (std::size_t column = 0; column < width; ++column) {
      const std::size_t chroma = ((row / 2) * chromaWidth) + (column / 2);
      const Rgb pixel =
          toRgb(picture.luma[(row * width) + column], picture.blue[chroma], picture.red[chroma]);
      const std::size_t at = ((row * width) + column) * 4;
      rgba[at] = pixel.r;
      rgba[at + 1] = pixel.g;
      rgba[at + 2] = pixel.b;
      rgba[at + 3] = std::byte{0xFF};
    }
  }
}

} // namespace dwe::video
