#include <dwe/formats/video/media_files.hpp>

#include <algorithm>
#include <format>
#include <limits>
#include <string_view>

namespace dwe::formats::video {

namespace {

constexpr std::string_view FrameMarker = "FRAME\n";
constexpr std::size_t WavHeaderSize = 44;

void appendText(std::vector<std::byte>& out, std::string_view text) {
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
}

void appendPlane(std::vector<std::byte>& out, std::span<const std::uint8_t> plane) {
  const auto bytes = std::as_bytes(plane);
  out.insert(out.end(), bytes.begin(), bytes.end());
}

void appendLittleEndian(std::vector<std::byte>& out, std::uint32_t value, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
  }
}

} // namespace

std::string y4mHeader(const Y4mFormat& format) {
  return std::format("YUV4MPEG2 W{} H{} F{}:{} Ip A1:1 {} XCOLORRANGE=FULL\n", format.width,
                     format.height, format.fpsNumerator, format.fpsDenominator,
                     format.bitDepth == 10 ? "C420p10" : "C420jpeg");
}

std::uint16_t tenBitLuma(int precise) {
  constexpr int Scale = 1023;
  const int clamped = std::clamp(precise, 0, PreciseMax);
  return static_cast<std::uint16_t>(((clamped * Scale) + (PreciseMax / 2)) / PreciseMax);
}

std::uint16_t tenBitChroma(int precise) {
  constexpr int Scale = 1023;
  constexpr int Grey = 128 * PreciseOne;
  constexpr int TenBitGrey = 512;
  // Rounded half away from zero, so grey's both sides are symmetric.
  const int offset = (std::clamp(precise, 0, PreciseMax) - Grey) * Scale;
  const int rounded = offset >= 0 ? (offset + (PreciseMax / 2)) / PreciseMax
                                  : -((-offset + (PreciseMax / 2)) / PreciseMax);
  return static_cast<std::uint16_t>(std::clamp(TenBitGrey + rounded, 0, 1023));
}

core::Result<void> appendY4mFrame(const Y4mFormat& format, const YCbCrFrame& frame,
                                  std::vector<std::byte>& out) {
  const std::size_t luma = std::size_t{format.width} * format.height;
  if (frame.width != format.width || frame.height != format.height || frame.y.size() != luma ||
      frame.cb.size() != luma / 4 || frame.cr.size() != luma / 4) {
    return core::fail(std::format("a {}x{} frame does not fit a {}x{} stream", frame.width,
                                  frame.height, format.width, format.height));
  }
  out.reserve(out.size() + FrameMarker.size() + luma + (luma / 2));
  appendText(out, FrameMarker);
  appendPlane(out, frame.y);
  appendPlane(out, frame.cb);
  appendPlane(out, frame.cr);
  return {};
}

core::Result<void> appendY4mFrame(const Y4mFormat& format, const PreciseFrame& frame,
                                  std::vector<std::byte>& out) {
  const std::size_t luma = std::size_t{format.width} * format.height;
  if (format.bitDepth != 10 || frame.width != format.width || frame.height != format.height ||
      frame.y.size() != luma || frame.cb.size() != luma / 4 || frame.cr.size() != luma / 4) {
    return core::fail(std::format("a {}x{} precise frame does not fit a {}x{} {}-bit stream",
                                  frame.width, frame.height, format.width, format.height,
                                  format.bitDepth));
  }
  out.reserve(out.size() + FrameMarker.size() + (3 * luma));
  appendText(out, FrameMarker);
  const auto append = [&](std::span<const std::int16_t> plane, auto convert) {
    for (const std::int16_t sample : plane) {
      appendLittleEndian(out, convert(sample), 2);
    }
  };
  append(frame.y, tenBitLuma);
  append(frame.cb, tenBitChroma);
  append(frame.cr, tenBitChroma);
  return {};
}

core::Result<std::vector<std::byte>> encodeWav(std::span<const std::int16_t> samples,
                                               std::uint16_t channels, std::uint32_t sampleRate) {
  if (channels == 0 || samples.size() % channels != 0) {
    return core::fail(
        std::format("{} samples do not split into {} channels", samples.size(), channels));
  }
  const std::size_t dataSize = samples.size() * 2;
  if (dataSize > std::numeric_limits<std::uint32_t>::max() - WavHeaderSize) {
    return core::fail("the audio is too long for a WAVE file");
  }
  std::vector<std::byte> out;
  out.reserve(WavHeaderSize + dataSize);
  appendText(out, "RIFF");
  appendLittleEndian(out, static_cast<std::uint32_t>(WavHeaderSize - 8 + dataSize), 4);
  appendText(out, "WAVEfmt ");
  appendLittleEndian(out, 16, 4); // size of the fmt chunk
  appendLittleEndian(out, 1, 2);  // PCM
  appendLittleEndian(out, channels, 2);
  appendLittleEndian(out, sampleRate, 4);
  appendLittleEndian(out, sampleRate * channels * 2U, 4); // bytes per second
  appendLittleEndian(out, channels * 2U, 2);              // bytes per sample frame
  appendLittleEndian(out, 16, 2);                         // bits per sample
  appendText(out, "data");
  appendLittleEndian(out, static_cast<std::uint32_t>(dataSize), 4);
  for (const std::int16_t sample : samples) {
    appendLittleEndian(out, static_cast<std::uint16_t>(sample), 2);
  }
  return out;
}

} // namespace dwe::formats::video
