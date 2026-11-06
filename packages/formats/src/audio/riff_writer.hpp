#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::formats::audio {

// Writes RIFF files (WAV, SF2): little-endian fields and chunks of a four-character id, a u32 size
// and the data, padded to an even size.
class RiffWriter {
public:
  void u8(std::uint32_t value) { bytes_.push_back(static_cast<std::byte>(value & 0xFFU)); }
  void u16(std::uint32_t value) {
    u8(value);
    u8(value >> 8U);
  }
  void u32(std::uint32_t value) {
    u16(value);
    u16(value >> 16U);
  }
  void i16(std::int32_t value) { u16(static_cast<std::uint32_t>(value)); }

  // Writes text into a field of fieldSize bytes, cut to leave at least one NUL and padded with
  // NULs.
  void name(std::string_view text, std::size_t fieldSize) {
    const std::size_t length = text.size() < fieldSize ? text.size() : fieldSize - 1;
    for (std::size_t i = 0; i < fieldSize; ++i) {
      u8(i < length ? static_cast<unsigned char>(text[i]) : 0U);
    }
  }

  void fourCc(std::string_view id) {
    for (const char c : id.substr(0, 4)) {
      u8(static_cast<unsigned char>(c));
    }
  }

  // Starts a chunk and returns where its size goes, for endChunk.
  std::size_t beginChunk(std::string_view id) {
    fourCc(id);
    const std::size_t sizeAt = bytes_.size();
    u32(0);
    return sizeAt;
  }

  // Starts a RIFF or LIST chunk of a form type.
  std::size_t beginList(std::string_view id, std::string_view type) {
    const std::size_t sizeAt = beginChunk(id);
    fourCc(type);
    return sizeAt;
  }

  // Writes the size of the chunk started at sizeAt and pads it to an even size.
  void endChunk(std::size_t sizeAt) {
    const auto size = static_cast<std::uint32_t>(bytes_.size() - sizeAt - 4);
    for (std::size_t i = 0; i < 4; ++i) {
      bytes_[sizeAt + i] = static_cast<std::byte>((size >> (8U * i)) & 0xFFU);
    }
    if (size % 2 != 0) {
      u8(0);
    }
  }

  // Writes a whole chunk with a 16-bit PCM payload.
  void pcmChunk(std::string_view id, std::span<const std::int16_t> pcm) {
    const std::size_t sizeAt = beginChunk(id);
    bytes_.reserve(bytes_.size() + (pcm.size() * 2));
    for (const auto sample : pcm) {
      i16(sample);
    }
    endChunk(sizeAt);
  }

  [[nodiscard]] std::size_t size() const { return bytes_.size(); }

  std::vector<std::byte> take() { return std::move(bytes_); }

private:
  std::vector<std::byte> bytes_;
};

} // namespace dwe::formats::audio
