#include <dwe/net/wire.hpp>

#include <algorithm>
#include <limits>

namespace dwe::net {

WireWriter& WireWriter::put(std::string_view text) {
  const std::size_t length =
      std::min<std::size_t>(text.size(), std::numeric_limits<std::uint8_t>::max());
  put(static_cast<std::uint8_t>(length));
  bytes_.insert(bytes_.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(length));
  return *this;
}

WireWriter& WireWriter::put(std::span<const std::uint8_t> bytes) {
  bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  return *this;
}

std::string WireReader::text() {
  const auto length = get<std::uint8_t>();
  if (bytes_.size() - at_ < length) {
    ok_ = false;
    at_ = bytes_.size();
    return {};
  }
  std::string text{bytes_.begin() + static_cast<std::ptrdiff_t>(at_),
                   bytes_.begin() + static_cast<std::ptrdiff_t>(at_ + length)};
  at_ += length;
  return text;
}

std::span<const std::uint8_t> WireReader::rest() {
  const std::span<const std::uint8_t> rest = bytes_.subspan(at_);
  at_ = bytes_.size();
  return rest;
}

} // namespace dwe::net
