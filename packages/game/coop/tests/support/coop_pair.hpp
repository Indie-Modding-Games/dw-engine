#pragma once

#include <dwe/game/coop/link.hpp>
#include <dwe/net/loopback.hpp>

#include <string>
#include <vector>

namespace dwe::game::coop::test {

inline Identity pairIdentity() {
  return {.protocol = ProtocolVersion, .gameVersion = "test", .contentHash = "content"};
}

struct CoopPairDesc {
  net::LinkConditions conditions{};
  std::string hostName = "Host";
  std::string guestName = "Guest";
  LinkTimeouts timeouts{};
  std::uint32_t seed = 1;
};

// A host and one guest in one process, their CoopLinks over a simulated network (LoopbackNetwork)
// with its own clock. Nothing is sent before connect(); pump() moves the clock and polls both.
class CoopPair {
public:
  explicit CoopPair(const CoopPairDesc& desc = {})
      : network_{desc.conditions, desc.seed},
        host_{CoopLink::host(network_.link(0), pairIdentity(), desc.hostName, desc.timeouts)},
        guest_{CoopLink::guest(network_.link(1), pairIdentity(), desc.guestName, desc.timeouts)} {}

  [[nodiscard]] CoopLink& host() { return host_; }
  [[nodiscard]] CoopLink& guest() { return guest_; }
  [[nodiscard]] net::LoopbackNetwork& network() { return network_; }
  [[nodiscard]] net::Millis now() const { return network_.now(); }

  // Moves the clock `steps` times by `step`, polling both links after each.
  void pump(int steps = 8, net::Millis step = net::Millis{1}) {
    for (int i = 0; i < steps; ++i) {
      network_.advance(step);
      host_.poll(network_.now());
      guest_.poll(network_.now());
    }
  }

  // Pumps until both links are ready, at most `limit` steps; whether they are.
  bool connect(int limit = 64) {
    for (int i = 0; i < limit && !ready(); ++i) {
      pump(1);
    }
    return ready();
  }

  [[nodiscard]] bool ready() const {
    return host_.phase() == LinkPhase::Ready && guest_.phase() == LinkPhase::Ready;
  }

  // Sends from one side and pumps until the other can receive it.
  void deliver(CoopLink& from, const Message& message, int steps = 8) {
    static_cast<void>(from.send(message));
    pump(steps);
  }

  // Everything a side received so far, in order.
  static std::vector<Message> drain(CoopLink& link) {
    std::vector<Message> out;
    while (auto message = link.receive()) {
      out.push_back(std::move(*message));
    }
    return out;
  }

  // Nothing travels either way from now on: a peer whose process ended without a word.
  void cut() { network_.cut(); }

private:
  net::LoopbackNetwork network_;
  CoopLink host_;
  CoopLink guest_;
};

} // namespace dwe::game::coop::test
