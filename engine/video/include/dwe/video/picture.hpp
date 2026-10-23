#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dwe::video {

// A decoded picture in planar 4:2:0 YCbCr, full range: one chroma sample for each 2x2 block of luma
// samples, as the movies are extracted.
struct YuvPicture {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> luma;
  std::vector<std::uint8_t> blue; // Cb
  std::vector<std::uint8_t> red;  // Cr

  // The size of each chroma plane for a picture of this size.
  [[nodiscard]] static std::size_t chromaSize(std::uint32_t width, std::uint32_t height);
};

// Converts a picture to 8-bit RGBA, rows from the top, alpha opaque, with the colour conversion
// of the original's decoder hardware. rgba is resized to width * height * 4.
void toRgba(const YuvPicture& picture, std::vector<std::byte>& rgba);

} // namespace dwe::video
