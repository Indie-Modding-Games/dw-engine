#include "ogg.hpp"

#include <array>
#include <format>
#include <string_view>

namespace dwe::assets {

namespace {

constexpr std::size_t PageHeaderSize = 27;
constexpr std::size_t CrcOffset = 22;
constexpr std::uint8_t Continued = 0x01;
constexpr std::uint8_t FirstPage = 0x02;
constexpr std::uint8_t LastPage = 0x04;
constexpr std::uint8_t LacingEnd = 255; // a lacing value below this ends a packet
constexpr std::uint32_t CrcPolynomial = 0x04C11DB7;

constexpr std::array<std::uint32_t, 256> CrcTable = [] {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < table.size(); ++i) {
    std::uint32_t value = i << 24U;
    for (int bit = 0; bit < 8; ++bit) {
      value = (value & 0x8000'0000U) != 0 ? (value << 1U) ^ CrcPolynomial : value << 1U;
    }
    table[i] = value;
  }
  return table;
}();

template <typename T>
T fieldAt(std::span<const std::byte> bytes, std::size_t offset) {
  T value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value = static_cast<T>(
        value | (static_cast<T>(std::to_integer<std::uint8_t>(bytes[offset + i])) << (8U * i)));
  }
  return value;
}

// The Ogg CRC-32 of a page, with its own CRC field taken as zero.
std::uint32_t pageCrc(std::span<const std::byte> page) {
  std::uint32_t crc = 0;
  for (std::size_t i = 0; i < page.size(); ++i) {
    const auto byte =
        i >= CrcOffset && i < CrcOffset + 4 ? 0U : std::to_integer<std::uint32_t>(page[i]);
    crc = (crc << 8U) ^ CrcTable[((crc >> 24U) ^ byte) & 0xFFU];
  }
  return crc;
}

} // namespace

core::Result<OggStream> readOggStream(std::span<const std::byte> bytes) {
  OggStream stream;
  std::vector<std::byte> pending; // a packet that continues on the next page
  bool ended = false;
  std::uint32_t sequence = 0;
  for (std::size_t at = 0; at < bytes.size();) {
    const auto failure = [&](std::string_view problem) {
      return core::fail(std::format("the Ogg page at {} {}", at, problem));
    };
    if (ended) {
      return failure("follows the end of the stream");
    }
    if (bytes.size() - at < PageHeaderSize) {
      return failure("is cut short");
    }
    const auto header = bytes.subspan(at, PageHeaderSize);
    if (std::string_view{reinterpret_cast<const char*>(header.data()), 4} != "OggS" ||
        std::to_integer<std::uint8_t>(header[4]) != 0) {
      return failure("does not start with OggS version 0");
    }
    const auto type = std::to_integer<std::uint8_t>(header[5]);
    const auto segments = std::to_integer<std::size_t>(header[26]);
    if (bytes.size() - at - PageHeaderSize < segments) {
      return failure("is cut short");
    }
    const auto lacing = bytes.subspan(at + PageHeaderSize, segments);
    std::size_t bodySize = 0;
    for (const std::byte value : lacing) {
      bodySize += std::to_integer<std::size_t>(value);
    }
    const std::size_t pageSize = PageHeaderSize + segments + bodySize;
    if (bytes.size() - at < pageSize) {
      return failure("is cut short");
    }
    const auto page = bytes.subspan(at, pageSize);
    if (pageCrc(page) != fieldAt<std::uint32_t>(page, CrcOffset)) {
      return failure("does not match its CRC");
    }
    const bool first = at == 0;
    if (((type & FirstPage) != 0) != first) {
      return failure(first ? "does not start the stream" : "starts a second stream");
    }
    const auto serial = fieldAt<std::uint32_t>(page, 14);
    if (first) {
      stream.serial = serial;
    } else if (serial != stream.serial || fieldAt<std::uint32_t>(page, 18) != sequence) {
      return failure("is not the next page of the stream");
    }
    if (((type & Continued) != 0) != !pending.empty()) {
      return failure(pending.empty() ? "continues a packet that is not there"
                                     : "drops a packet halfway");
    }
    std::size_t offset = PageHeaderSize + segments;
    for (const std::byte value : lacing) {
      const auto size = std::to_integer<std::size_t>(value);
      const auto segment = page.subspan(offset, size);
      pending.insert(pending.end(), segment.begin(), segment.end());
      offset += size;
      if (size < LacingEnd) {
        stream.packets.push_back(std::move(pending));
        pending.clear();
      }
    }
    // -1: no packet ends on this page.
    if (const auto granule = static_cast<std::int64_t>(fieldAt<std::uint64_t>(page, 6));
        granule != -1) {
      stream.lastGranule = granule;
    }
    ended = (type & LastPage) != 0;
    sequence = fieldAt<std::uint32_t>(page, 18) + 1;
    at += pageSize;
  }
  if (!ended || !pending.empty()) {
    return core::fail("the Ogg stream does not end");
  }
  return stream;
}

} // namespace dwe::assets
