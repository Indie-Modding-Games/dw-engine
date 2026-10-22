#pragma once

#include <dwe/core/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// CD-ROM XA ADPCM audio, the sound of interleaved streams: each form 2 audio sector holds 18 sound
// groups of 128 bytes (16 bytes of parameters, then 28 words of packed samples), which the drive
// decodes itself.
namespace dwe::formats::video {

// Sound groups in a sector and bytes each one takes; the 20 bytes after them are unused.
constexpr std::size_t XaSoundGroups = 18;
constexpr std::size_t XaSoundGroupSize = 128;
// Samples each sound unit gives.
constexpr std::size_t XaSamplesPerUnit = 28;

// How an audio sector is coded: the coding byte of its subheader.
struct XaCoding {
  std::uint8_t channels;      // 1 or 2 (bit 0)
  std::uint32_t sampleRate;   // 37800 or 18900 Hz (bit 2)
  std::uint8_t bitsPerSample; // 4 or 8 (bit 4)

  bool operator==(const XaCoding&) const = default;
};

// Reads a coding byte. Reserved values (bits 1, 3, 5 and 7) and emphasis (bit 6), which the decoder
// does not apply, are errors.
core::Result<XaCoding> readXaCoding(std::uint8_t coding);

// Returns the sample frames (one sample per channel) a sector gives: 4032 samples over the channels
// at 4 bits, 2016 at 8 bits.
std::size_t xaSampleFramesPerSector(const XaCoding& coding);

// Decodes the sectors of one stream in order. Each channel keeps the last two samples of its
// previous sound unit, across sound groups and sectors, so the sectors of a stream must go through
// the same decoder.
class XaDecoder {
public:
  // Decodes the sound groups of one sector's user data (at least 18 x 128 bytes) and appends 16-bit
  // samples to out, interleaved left and right when stereo. Returns how many sound units have a
  // parameter byte that differs from its copy in the group header; the decoder uses the first one.
  core::Result<std::size_t> decodeSector(std::span<const std::byte> data, const XaCoding& coding,
                                         std::vector<std::int16_t>& out);

private:
  // Per channel: the previous sample and the one before it.
  std::array<std::array<std::int32_t, 2>, 2> history_{};
};

} // namespace dwe::formats::video
