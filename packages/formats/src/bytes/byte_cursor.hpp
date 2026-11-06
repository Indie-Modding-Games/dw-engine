#pragma once

#include <dwe/formats/byte_reader.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace dwe::formats::bytes {

// Reads little-endian fields one after another through a ByteReader. A read past the end gives 0
// and marks the cursor as overrun, so a parser can read a whole record and check once.
class ByteCursor {
public:
  explicit ByteCursor(std::span<const std::byte> bytes, std::size_t offset = 0)
      : reader_(bytes), offset_(offset) {}

  [[nodiscard]] std::size_t offset() const { return offset_; }
  [[nodiscard]] bool overrun() const { return overrun_; }
  [[nodiscard]] std::size_t remaining() const {
    return overrun_ || offset_ > reader_.size() ? 0 : reader_.size() - offset_;
  }

  std::uint8_t u8() { return next(reader_.u8(offset_)); }
  std::int8_t i8() { return static_cast<std::int8_t>(u8()); }
  std::uint16_t u16() { return next(reader_.u16(offset_)); }
  std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
  std::uint32_t u32() { return next(reader_.u32(offset_)); }
  std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

  // Returns the next count bytes, or an empty span (and marks the overrun) if there are not that
  // many.
  std::span<const std::byte> take(std::size_t count) {
    const auto taken = overrun_ ? std::nullopt : reader_.bytes(offset_, count);
    if (!taken) {
      overrun_ = true;
      return {};
    }
    offset_ += count;
    return *taken;
  }

  void skip(std::size_t count) { take(count); }

private:
  template <typename T>
  T next(std::optional<T> value) {
    if (overrun_ || !value) {
      overrun_ = true;
      return T{};
    }
    offset_ += sizeof(T);
    return *value;
  }

  ByteReader reader_;
  std::size_t offset_;
  bool overrun_ = false;
};

} // namespace dwe::formats::bytes
