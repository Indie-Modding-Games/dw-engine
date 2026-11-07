#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/video/picture.hpp>
#include <dwe/video/y4m_reader.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace dwe::video {

// Reads the AV1 video of a cooked movie (an IVF file, as dwx cook writes it) frame by frame from a
// file read in pieces, decoding it with libaom: only the frame being decoded is in memory. Only
// 8-bit 4:2:0 streams, as the movies are cooked.
class Av1Reader {
public:
  // Reads and checks the IVF header and starts the decoder. name is for the errors.
  static core::Result<Av1Reader> open(std::unique_ptr<assets::FileReader> file, std::string name);

  Av1Reader(Av1Reader&&) noexcept;
  Av1Reader& operator=(Av1Reader&&) noexcept;
  Av1Reader(const Av1Reader&) = delete;
  Av1Reader& operator=(const Av1Reader&) = delete;
  ~Av1Reader();

  // The size and rate, as the IVF header says.
  [[nodiscard]] const Y4mStream& stream() const { return stream_; }

  // Decodes the next frame into picture; false at the end of the stream.
  core::Result<bool> read(YuvPicture& picture);
  // Decodes the next frame without keeping it (the frames after it are built from it); false at
  // the end of the stream.
  core::Result<bool> skip();

private:
  struct Decoder;

  Av1Reader(std::unique_ptr<assets::FileReader> file, std::string name, Y4mStream stream,
            std::unique_ptr<Decoder> decoder);

  // Feeds the decoder until it gives a picture; false when the file ends first. picture may be
  // null to only advance.
  core::Result<bool> decodeNext(YuvPicture* picture);

  std::unique_ptr<assets::FileReader> file_;
  std::string name_;
  Y4mStream stream_;
  std::unique_ptr<Decoder> decoder_;
  std::vector<std::byte> frame_;
};

} // namespace dwe::video
