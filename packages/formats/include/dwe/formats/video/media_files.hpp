#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/video/mdec.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The open files the extractor writes for a movie: uncompressed YUV4MPEG2 video and 16-bit PCM WAVE
// audio. Neither needs a library, and the same input always gives the same bytes.
namespace dwe::formats::video {

// What a YUV4MPEG2 stream holds.
struct Y4mFormat {
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t fpsNumerator;
  std::uint32_t fpsDenominator;
  // 8: the hardware's planes as they are (C420jpeg); 10: precise planes (PreciseFrame) as 16-bit
  // little-endian samples (C420p10).
  std::uint32_t bitDepth = 8;
};

// Returns the stream header: 4:2:0 with each chroma sample centred on its 2x2 luma samples
// (C420jpeg; the MDEC repeats it over the four), progressive, square pixels and full range
// (XCOLORRANGE=FULL, as ffmpeg reads it). A 10-bit stream says C420p10, with the same siting.
std::string y4mHeader(const Y4mFormat& format);

// Appends one frame of the stream: "FRAME", then the Y, Cb and Cr planes as decoded, without any
// conversion. The frame must have the stream's size.
core::Result<void> appendY4mFrame(const Y4mFormat& format, const YCbCrFrame& frame,
                                  std::vector<std::byte>& out);

// Appends one frame of a 10-bit stream from precise planes: each sixteenth-of-a-level sample
// rescaled to the full 10-bit range as H.273 defines it (luma 0..1023, chroma centred on 512, both
// times 1023 / 255) and rounded. The frame must have the stream's size.
core::Result<void> appendY4mFrame(const Y4mFormat& format, const PreciseFrame& frame,
                                  std::vector<std::byte>& out);

// The 10-bit full-range code of a precise luma or chroma sample.
std::uint16_t tenBitLuma(int precise);
std::uint16_t tenBitChroma(int precise);

// Returns a WAVE file of 16-bit little-endian PCM; samples are interleaved by channel.
core::Result<std::vector<std::byte>> encodeWav(std::span<const std::int16_t> samples,
                                               std::uint16_t channels, std::uint32_t sampleRate);

} // namespace dwe::formats::video
