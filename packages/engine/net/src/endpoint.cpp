#include <dwe/net/endpoint.hpp>

#include <cctype>
#include <charconv>
#include <cstddef>
#include <format>
#include <string>

namespace dwe::net {

namespace {

constexpr std::string_view Alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr std::size_t CodeLength = 10;
constexpr std::size_t CodeGroup = 5;
constexpr std::uint32_t BitsPerLetter = 5;
constexpr std::uint32_t LetterMask = 0x1FU;
constexpr std::uint32_t ByteBits = 8;
constexpr std::uint32_t MaxOctet = 255;

std::uint64_t packed(const Endpoint& endpoint) {
  std::uint64_t value = 0;
  for (const std::uint8_t octet : endpoint.address) {
    value = (value << ByteBits) | octet;
  }
  return (value << (2 * ByteBits)) | endpoint.port;
}

Endpoint unpacked(std::uint64_t value) {
  Endpoint endpoint;
  endpoint.port = static_cast<std::uint16_t>(value & 0xFFFFU);
  value >>= 2 * ByteBits;
  for (std::size_t octet = endpoint.address.size(); octet-- > 0;) {
    endpoint.address.at(octet) = static_cast<std::uint8_t>(value & 0xFFU);
    value >>= ByteBits;
  }
  return endpoint;
}

std::optional<std::uint32_t> letterValue(char letter) {
  const auto upper = static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
  if (upper == 'I' || upper == 'L') {
    return 1;
  }
  if (upper == 'O') {
    return 0;
  }
  const std::size_t found = Alphabet.find(upper);
  if (found == std::string_view::npos) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(found);
}

} // namespace

std::string formatEndpoint(const Endpoint& endpoint) {
  const auto& a = endpoint.address;
  return std::format("{}.{}.{}.{}:{}", a[0], a[1], a[2], a[3], endpoint.port);
}

core::Result<Endpoint> parseEndpoint(std::string_view text) {
  const auto bad = [&] {
    return core::fail(
        std::format("'{}' is not an IPv4 address with its port (a.b.c.d:port)", text));
  };
  Endpoint endpoint;
  const char* at = text.data();
  const char* end = text.data() + text.size();
  for (std::size_t octet = 0; octet < endpoint.address.size(); ++octet) {
    std::uint32_t value = 0;
    const auto [next, error] = std::from_chars(at, end, value);
    const char separator = octet + 1 < endpoint.address.size() ? '.' : ':';
    if (error != std::errc{} || value > MaxOctet || next == end || *next != separator) {
      return bad();
    }
    endpoint.address.at(octet) = static_cast<std::uint8_t>(value);
    at = next + 1;
  }
  std::uint32_t port = 0;
  const auto [next, error] = std::from_chars(at, end, port);
  if (error != std::errc{} || next != end || port == 0 || port > 0xFFFFU) {
    return bad();
  }
  endpoint.port = static_cast<std::uint16_t>(port);
  return endpoint;
}

std::string joinCodeOf(const Endpoint& endpoint) {
  const std::uint64_t value = packed(endpoint);
  std::string code;
  for (std::size_t letter = 0; letter < CodeLength; ++letter) {
    if (letter == CodeGroup) {
      code.push_back('-');
    }
    const auto shift = static_cast<std::uint32_t>((CodeLength - 1 - letter) * BitsPerLetter);
    code.push_back(Alphabet[(value >> shift) & LetterMask]);
  }
  return code;
}

core::Result<Endpoint> parseJoinCode(std::string_view code) {
  std::uint64_t value = 0;
  std::size_t letters = 0;
  for (const char letter : code) {
    if (letter == '-' || letter == ' ') {
      continue;
    }
    const std::optional<std::uint32_t> digit = letterValue(letter);
    if (!digit || ++letters > CodeLength) {
      return core::fail(std::format("'{}' is not a join code", code));
    }
    value = (value << BitsPerLetter) | *digit;
  }
  // Ten letters hold 50 bits: the two on top must be clear.
  if (letters != CodeLength || (value >> (CodeLength * BitsPerLetter - 2)) != 0) {
    return core::fail(std::format("'{}' is not a join code", code));
  }
  Endpoint endpoint = unpacked(value);
  if (endpoint.port == 0) {
    return core::fail(std::format("'{}' is not a join code", code));
  }
  return endpoint;
}

core::Result<Endpoint> parseJoinTarget(std::string_view text) {
  while (!text.empty() && text.front() == ' ') {
    text.remove_prefix(1);
  }
  while (!text.empty() && text.back() == ' ') {
    text.remove_suffix(1);
  }
  if (text.find('.') != std::string_view::npos || text.find(':') != std::string_view::npos) {
    return parseEndpoint(text);
  }
  return parseJoinCode(text);
}

} // namespace dwe::net
