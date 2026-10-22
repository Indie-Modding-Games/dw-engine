#pragma once

#include <dwe/formats/image/tim.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

// Builds synthetic TIMs for the tests, so no fixture comes from the disc.
namespace dwe::formats::image::test {

inline void putU16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFU));
  out.push_back(static_cast<std::byte>(value >> 8U));
}

inline void putU32(std::vector<std::byte>& out, std::uint32_t value) {
  putU16(out, static_cast<std::uint16_t>(value & 0xFFFFU));
  putU16(out, static_cast<std::uint16_t>(value >> 16U));
}

struct TimSpec {
  TimPixelMode mode = TimPixelMode::Direct16;
  std::optional<VramRect> clutRect;
  std::vector<Color15> clut;
  VramRect imageRect;
  std::vector<std::byte> data;
  std::optional<std::uint32_t> clutLength; // overrides the computed block lengths
  std::optional<std::uint32_t> imageLength;
};

// A TIM without CLUT.
inline TimSpec directTim(TimPixelMode mode, VramRect imageRect, std::vector<std::byte> data) {
  return TimSpec{.mode = mode,
                 .clutRect = std::nullopt,
                 .clut = {},
                 .imageRect = imageRect,
                 .data = std::move(data),
                 .clutLength = std::nullopt,
                 .imageLength = std::nullopt};
}

// A TIM with a CLUT.
inline TimSpec indexedTim(TimPixelMode mode, VramRect clutRect, std::vector<Color15> clut,
                          VramRect imageRect, std::vector<std::byte> data) {
  return TimSpec{.mode = mode,
                 .clutRect = clutRect,
                 .clut = std::move(clut),
                 .imageRect = imageRect,
                 .data = std::move(data),
                 .clutLength = std::nullopt,
                 .imageLength = std::nullopt};
}

inline void appendBlock(std::vector<std::byte>& out, std::optional<std::uint32_t> length,
                        const VramRect& rect, std::size_t dataSize) {
  putU32(out, length.value_or(static_cast<std::uint32_t>(12 + dataSize)));
  putU16(out, rect.x);
  putU16(out, rect.y);
  putU16(out, rect.width);
  putU16(out, rect.height);
}

inline void appendTim(std::vector<std::byte>& out, const TimSpec& spec) {
  putU32(out, TimMagic);
  putU32(out, static_cast<std::uint32_t>(spec.mode) | (spec.clutRect ? 0x8U : 0U));
  if (spec.clutRect) {
    appendBlock(out, spec.clutLength, *spec.clutRect, spec.clut.size() * 2);
    for (const Color15 color : spec.clut) {
      putU16(out, color);
    }
  }
  appendBlock(out, spec.imageLength, spec.imageRect, spec.data.size());
  out.insert(out.end(), spec.data.begin(), spec.data.end());
}

inline std::vector<std::byte> buildTim(const TimSpec& spec) {
  std::vector<std::byte> out;
  appendTim(out, spec);
  return out;
}

inline std::vector<std::byte> bytesOf(std::initializer_list<unsigned> values) {
  std::vector<std::byte> out;
  for (const unsigned value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

} // namespace dwe::formats::image::test
