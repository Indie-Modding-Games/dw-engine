#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/video/picture.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace dwe::video {

// What a YUV4MPEG2 stream says about itself.
struct Y4mStream {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t fpsNumerator = 0;
  std::uint32_t fpsDenominator = 1;
  // 8, or 10 for C420p10 (16-bit little-endian samples).
  unsigned bitDepth = 8;
};

// Reads a YUV4MPEG2 (.y4m) video frame by frame from a file read in pieces: only the frame being
// read is in memory. Only 8- and 10-bit 4:2:0 progressive streams, as dwx extract writes the
// movies.
class Y4mReader {
public:
  // Reads and checks the stream header. name is for the errors.
  static core::Result<Y4mReader> open(std::unique_ptr<assets::FileReader> file, std::string name);

  [[nodiscard]] const Y4mStream& stream() const { return stream_; }

  // Reads the next frame into picture; false at the end of the stream.
  core::Result<bool> read(YuvPicture& picture);
  // Passes over the next frame without keeping it; false at the end of the stream.
  core::Result<bool> skip();

private:
  Y4mReader(std::unique_ptr<assets::FileReader> file, std::string name, Y4mStream stream)
      : file_(std::move(file)), name_(std::move(name)), stream_(stream) {}

  // Reads the "FRAME" line; false when the stream ends cleanly before it.
  core::Result<bool> readFrameHeader();
  core::Result<void> readExactly(std::span<std::byte> into);

  std::unique_ptr<assets::FileReader> file_;
  std::string name_;
  Y4mStream stream_;
  std::vector<std::byte> buffer_;
};

} // namespace dwe::video
