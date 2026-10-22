#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/sector.hpp>
#include <dwe/formats/video/xa_adpcm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

// STR stream files: mode 2 sectors read at a fixed rate, where video sectors (form 1, a 32-byte
// StHEADER and a chunk of a frame's bitstream) are interleaved with XA audio sectors (form 2). A
// file must be read as raw sectors with their subheaders; the 2048-byte view of an ISO image loses
// the audio and the headers' sector types.
namespace dwe::formats::video {

// Bits of a sector's submode.
namespace submode {
constexpr std::uint8_t EndOfRecord = 0x01;
constexpr std::uint8_t Video = 0x02;
constexpr std::uint8_t Audio = 0x04;
constexpr std::uint8_t Data = 0x08;
constexpr std::uint8_t Form2 = 0x20;
constexpr std::uint8_t RealTime = 0x40;
constexpr std::uint8_t EndOfFile = 0x80;
} // namespace submode

// Size of the StHEADER that starts every video sector, and of the frame data after it.
constexpr std::size_t StHeaderSize = 0x20;
constexpr std::size_t StChunkSize = 0x7E0;

// A video sector: its StHEADER (libpress) and its chunk of the frame.
struct StrVideoSector {
  std::uint16_t sectorInFrame;  // 0 for the first sector of a frame
  std::uint16_t sectorsInFrame; // how many sectors the frame takes
  std::uint32_t frameNumber;    // counts from 1
  std::uint32_t frameBytes; // bytes of the frame's bitstream; the rest of its last chunk is padding
  std::uint16_t width;
  std::uint16_t height;
  std::uint32_t headm;              // the first 4 bytes of the frame's bitstream
  std::uint32_t headv;              // the next 4
  std::span<const std::byte> chunk; // StChunkSize bytes
};

// An XA audio sector.
struct StrAudioSector {
  std::uint8_t file;
  std::uint8_t channel;
  XaCoding coding;
  bool endOfFile;
  std::span<const std::byte> data; // the 18 sound groups and the unused bytes after them
};

// A sector with no type bits in its submode and only zeros: padding at the end of a stream.
struct StrPaddingSector {};

using StrSector = std::variant<StrVideoSector, StrAudioSector, StrPaddingSector>;

// Tells what a sector of a stream file holds. Video needs form 1, the data or video submode bit and
// the StHEADER marker (0x0160, 0x8001) with a reserved field of 0; audio needs form 2, the audio
// bit and a known coding. Anything else, including a sector without a subheader, is an error.
core::Result<StrSector> readStrSector(const disc::SectorData& sector);

// A frame put together from its sectors.
struct StrFrame {
  std::uint32_t number;
  std::uint16_t width;
  std::uint16_t height;
  std::vector<std::byte> bitstream;
};

// Puts each frame together from its video sectors, which come in order, one frame after another
// (audio sectors in between do not matter). Checks that every sector of a frame agrees on its
// header, that the frame fits its sectors, that the padding after it is zero and that headm and
// headv repeat the start of the bitstream.
class StrFrameAssembler {
public:
  // Adds the next video sector. Returns the frame when this sector completes it, nullopt otherwise.
  // On an error the frame in progress is dropped; a first sector that interrupts a frame starts the
  // next one.
  core::Result<std::optional<StrFrame>> add(const StrVideoSector& sector);

  // The number of the frame in progress, if a frame is missing sectors.
  [[nodiscard]] std::optional<std::uint32_t> pendingFrame() const;

private:
  core::Result<std::optional<StrFrame>> take(const StrVideoSector& sector);
  void reset();

  std::optional<StrVideoSector> first_;
  std::vector<std::byte> data_;
  std::uint16_t nextSector_ = 0;
};

} // namespace dwe::formats::video
