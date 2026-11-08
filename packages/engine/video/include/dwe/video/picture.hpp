#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dwe::video {

// A decoded picture in planar 4:2:0 YCbCr, full range: one chroma sample for each 2x2 block of luma
// samples, centred on the four (as MPEG-1 and JPEG site it), as the movies are extracted. Samples
// are 8- or 10-bit codes (bitDepth) held in 16 bits.
struct YuvPicture {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  unsigned bitDepth = 8;
  std::vector<std::uint16_t> luma;
  std::vector<std::uint16_t> blue; // Cb
  std::vector<std::uint16_t> red;  // Cr

  // The size of each chroma plane for a picture of this size.
  [[nodiscard]] static std::size_t chromaSize(std::uint32_t width, std::uint32_t height);
};

// Converts a picture to 8-bit RGBA, rows from the top, alpha opaque: the chroma brought up to every
// pixel by bilinear interpolation from its centred samples (weights 3/4 and 1/4 on each axis,
// edges repeated) and converted with the exact full-range BT.601 matrix, which the original's
// decoder hardware approximates, rounded once. rgba is resized to width * height * 4.
void toRgba(const YuvPicture& picture, std::vector<std::byte>& rgba);

} // namespace dwe::video
