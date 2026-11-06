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
};

// Returns the stream header: 4:2:0 with each chroma sample centred on its 2x2 luma samples
// (C420jpeg; the MDEC repeats it over the four), progressive, square pixels and full range
// (XCOLORRANGE=FULL, as ffmpeg reads it).
std::string y4mHeader(const Y4mFormat& format);

// Appends one frame of the stream: "FRAME", then the Y, Cb and Cr planes as decoded, without any
// conversion. The frame must have the stream's size.
core::Result<void> appendY4mFrame(const Y4mFormat& format, const YCbCrFrame& frame,
                                  std::vector<std::byte>& out);

// Returns a WAVE file of 16-bit little-endian PCM; samples are interleaved by channel.
core::Result<std::vector<std::byte>> encodeWav(std::span<const std::int16_t> samples,
                                               std::uint16_t channels, std::uint32_t sampleRate);

} // namespace dwe::formats::video
