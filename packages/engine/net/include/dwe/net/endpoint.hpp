#pragma once

#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace dwe::net {

// An IPv4 address and a UDP port.
struct Endpoint {
  std::array<std::uint8_t, 4> address{};
  std::uint16_t port = 0;

  friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

// "a.b.c.d:port".
[[nodiscard]] std::string formatEndpoint(const Endpoint& endpoint);
[[nodiscard]] core::Result<Endpoint> parseEndpoint(std::string_view text);

// A short code a player reads out to the other one instead of the address: the six bytes of the
// endpoint in ten letters and digits of Crockford's base 32 (no I, L, O or U), as "ABCDE-12345".
// It hides nothing: anyone can read the address back from it.
[[nodiscard]] std::string joinCodeOf(const Endpoint& endpoint);
// Reads a join code back, ignoring case, spaces and dashes, and taking I and L for 1 and O for 0.
[[nodiscard]] core::Result<Endpoint> parseJoinCode(std::string_view code);
// A join code or an address, whichever the text is; an address without a port takes
// `defaultPort` (none: it needs one).
[[nodiscard]] core::Result<Endpoint> parseJoinTarget(std::string_view text,
                                                     std::uint16_t defaultPort = 0);

} // namespace dwe::net
