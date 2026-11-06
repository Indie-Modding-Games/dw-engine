#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace dwe::formats {

// Reads little-endian fields at offsets of a buffer. Every read is bounds-checked: one that does
// not fit in the buffer returns nothing instead of reading past it.
class ByteReader {
public:
  explicit constexpr ByteReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  [[nodiscard]] constexpr std::size_t size() const { return bytes_.size(); }

  // Returns true if count bytes starting at offset are inside the buffer.
  [[nodiscard]] constexpr bool contains(std::size_t offset, std::size_t count) const {
    return offset <= bytes_.size() && count <= bytes_.size() - offset;
  }

  [[nodiscard]] constexpr std::optional<std::uint8_t> u8(std::size_t offset) const {
    return unsignedAt<std::uint8_t>(offset);
  }

  [[nodiscard]] constexpr std::optional<std::uint16_t> u16(std::size_t offset) const {
    return unsignedAt<std::uint16_t>(offset);
  }

  [[nodiscard]] constexpr std::optional<std::uint32_t> u32(std::size_t offset) const {
    return unsignedAt<std::uint32_t>(offset);
  }

  [[nodiscard]] constexpr std::optional<std::int32_t> s32(std::size_t offset) const {
    const auto value = u32(offset);
    if (!value) {
      return std::nullopt;
    }
    return static_cast<std::int32_t>(*value);
  }

  // Returns count bytes starting at offset.
  [[nodiscard]] constexpr std::optional<std::span<const std::byte>> bytes(std::size_t offset,
                                                                          std::size_t count) const {
    if (!contains(offset, count)) {
      return std::nullopt;
    }
    return bytes_.subspan(offset, count);
  }

  // Returns count bytes starting at offset as characters.
  [[nodiscard]] std::optional<std::string_view> chars(std::size_t offset, std::size_t count) const {
    const auto span = bytes(offset, count);
    if (!span) {
      return std::nullopt;
    }
    // Viewing bytes as characters is the one reinterpretation the language allows for any object.
    return std::string_view{reinterpret_cast<const char*>(span->data()), span->size()};
  }

  // Returns the characters starting at offset up to the first NUL, which must come within maxLength
  // bytes and inside the buffer.
  [[nodiscard]] std::optional<std::string_view> cString(std::size_t offset,
                                                        std::size_t maxLength) const {
    if (offset > bytes_.size()) {
      return std::nullopt;
    }
    const std::size_t available = bytes_.size() - offset;
    const auto rest = bytes_.subspan(offset, maxLength < available ? maxLength + 1 : available);
    const auto nul = std::ranges::find(rest, std::byte{0});
    if (nul == rest.end()) {
      return std::nullopt;
    }
    return chars(offset, static_cast<std::size_t>(nul - rest.begin()));
  }

private:
  template <typename T>
  [[nodiscard]] constexpr std::optional<T> unsignedAt(std::size_t offset) const {
    if (!contains(offset, sizeof(T))) {
      return std::nullopt;
    }
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
      value = static_cast<T>(value | (static_cast<T>(bytes_[offset + i]) << (8U * i)));
    }
    return value;
  }

  std::span<const std::byte> bytes_;
};

} // namespace dwe::formats
