#pragma once

#include <dwe/net/datagram.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <random>

namespace dwe::net {

// What the loopback network does to the datagrams on their way, to test the code above it against
// a bad connection.
struct LinkConditions {
  // Every datagram takes this long, plus up to `jitter` more at random: with jitter they reorder.
  Millis latency{0};
  Millis jitter{0};
  // The share of the datagrams lost and of those that arrive twice, 0 to 100.
  std::int32_t lossPercent = 0;
  std::int32_t duplicatePercent = 0;
};

// Two peers in one process joined by a simulated network with its own clock: a datagram sent at
// time t arrives once the clock reaches t plus its delay. The same seed and the same calls play
// the same way.
class LoopbackNetwork {
public:
  explicit LoopbackNetwork(LinkConditions conditions, std::uint32_t seed = 1);
  ~LoopbackNetwork();
  LoopbackNetwork(const LoopbackNetwork&) = delete;
  LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;
  LoopbackNetwork(LoopbackNetwork&&) = delete;
  LoopbackNetwork& operator=(LoopbackNetwork&&) = delete;

  // The link of peer 0 or 1; it lives as long as the network.
  [[nodiscard]] DatagramLink& peer(std::size_t side);
  // Moves the clock on; the datagrams due by then can be received.
  void advance(Millis elapsed);
  [[nodiscard]] Millis now() const { return now_; }
  // Cuts the link: from now on nothing arrives either way (a peer that leaves without a word).
  void cut() { cut_ = true; }
  // Datagrams sent and lost so far.
  [[nodiscard]] std::size_t sent() const { return sent_; }
  [[nodiscard]] std::size_t lost() const { return lost_; }

private:
  class End;
  struct InFlight {
    Millis due{0};
    std::uint64_t order = 0;
    Bytes datagram;
  };
  void post(std::size_t from, std::span<const std::uint8_t> datagram);

  LinkConditions conditions_;
  std::mt19937 random_;
  Millis now_{0};
  bool cut_ = false;
  std::uint64_t order_ = 0;
  std::size_t sent_ = 0;
  std::size_t lost_ = 0;
  // The datagrams on their way to each side.
  std::array<std::deque<InFlight>, 2> toward_;
  std::array<std::unique_ptr<End>, 2> ends_;
};

} // namespace dwe::net
