#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dwe::net {

using Bytes = std::vector<std::uint8_t>;

// The time the network code works with: a steady clock in milliseconds, which the tests drive by
// hand.
using Millis = std::chrono::milliseconds;

// The largest datagram the network code sends: under the usual path MTU, so nothing is fragmented.
inline constexpr std::size_t MaxDatagram = 1200;

// One end of an unreliable datagram link between two peers: what is sent may arrive late, out of
// order, twice or never (UDP's guarantees). Nothing here blocks.
class DatagramLink {
public:
  DatagramLink() = default;
  DatagramLink(const DatagramLink&) = delete;
  DatagramLink& operator=(const DatagramLink&) = delete;
  DatagramLink(DatagramLink&&) = delete;
  DatagramLink& operator=(DatagramLink&&) = delete;
  virtual ~DatagramLink() = default;

  // Sends one datagram to the peer; dropped silently when there is no peer yet or the system
  // refuses it, as UDP would drop it on the way.
  virtual void send(std::span<const std::uint8_t> datagram) = 0;
  // The next datagram that arrived from the peer, if any.
  virtual std::optional<Bytes> receive() = 0;
};

} // namespace dwe::net
