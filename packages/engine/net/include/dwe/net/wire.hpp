#pragma once

#include <dwe/net/datagram.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace dwe::net {

// Writes numbers little-endian and texts with their length, so both machines read the same bytes
// whatever their own byte order.
class WireWriter {
public:
  template <typename T>
    requires std::is_integral_v<T>
  WireWriter& put(T value) {
    using Unsigned =
        std::make_unsigned_t<std::conditional_t<std::is_same_v<T, bool>, std::uint8_t, T>>;
    auto bits = static_cast<Unsigned>(value);
    for (std::size_t byte = 0; byte < sizeof(Unsigned); ++byte) {
      bytes_.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
      if constexpr (sizeof(Unsigned) > 1) {
        bits = static_cast<Unsigned>(bits >> 8U);
      }
    }
    return *this;
  }

  template <typename T>
    requires std::is_enum_v<T>
  WireWriter& put(T value) {
    return put(std::to_underlying(value));
  }

  // A text of up to 255 bytes; longer ones are cut.
  WireWriter& put(std::string_view text);
  WireWriter& put(std::span<const std::uint8_t> bytes);

  [[nodiscard]] const Bytes& bytes() const { return bytes_; }
  [[nodiscard]] Bytes take() { return std::move(bytes_); }

private:
  Bytes bytes_;
};

// Reads what WireWriter wrote. A read past the end gives zeros and marks the reader bad, so a
// short or damaged message is refused once instead of at each field.
class WireReader {
public:
  explicit WireReader(std::span<const std::uint8_t> bytes) : bytes_{bytes} {}

  template <typename T>
    requires std::is_integral_v<T>
  T get() {
    using Unsigned =
        std::make_unsigned_t<std::conditional_t<std::is_same_v<T, bool>, std::uint8_t, T>>;
    if (bytes_.size() - at_ < sizeof(Unsigned)) {
      ok_ = false;
      at_ = bytes_.size();
      return T{};
    }
    Unsigned bits = 0;
    for (std::size_t byte = 0; byte < sizeof(Unsigned); ++byte) {
      bits =
          static_cast<Unsigned>(bits | (static_cast<Unsigned>(bytes_[at_ + byte]) << (8U * byte)));
    }
    at_ += sizeof(Unsigned);
    return static_cast<T>(bits);
  }

  std::string text();
  // What is left, all of it.
  std::span<const std::uint8_t> rest();

  [[nodiscard]] bool ok() const { return ok_; }
  // Read to its end with nothing wrong.
  [[nodiscard]] bool done() const { return ok_ && at_ == bytes_.size(); }

private:
  std::span<const std::uint8_t> bytes_;
  std::size_t at_ = 0;
  bool ok_ = true;
};

} // namespace dwe::net
