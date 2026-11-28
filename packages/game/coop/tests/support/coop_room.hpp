#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "coop_pair.hpp"

namespace dwe::game::coop::test {

struct CoopRoomDesc {
  std::size_t guests = MaxPlayers - 1;
  net::LinkConditions conditions{};
  LinkTimeouts timeouts{};
  std::uint32_t seed = 1;
};

// A host and up to three guests in one process, as a room of four seats them: one simulated
// network and one host CoopLink per guest, each host link giving its guest the id of its seat
// (1 to 3). Nothing is sent before connect(); pump() moves every clock and polls every link.
class CoopRoom {
public:
  struct Seat {
    PlayerId id = GuestPlayerId;
    std::unique_ptr<net::LoopbackNetwork> network;
    std::unique_ptr<CoopLink> host;
    std::unique_ptr<CoopLink> guest;
  };

  explicit CoopRoom(const CoopRoomDesc& desc = {}) {
    for (std::size_t index = 0; index < desc.guests && index + 1 < MaxPlayers; ++index) {
      const auto id = static_cast<PlayerId>(index + 1);
      Seat seat{.id = id,
                .network = std::make_unique<net::LoopbackNetwork>(desc.conditions, desc.seed + id),
                .host = nullptr,
                .guest = nullptr};
      seat.host = std::make_unique<CoopLink>(
          CoopLink::host(seat.network->link(0), pairIdentity(), "Host", desc.timeouts, id));
      seat.guest = std::make_unique<CoopLink>(CoopLink::guest(
          seat.network->link(1), pairIdentity(), "Guest " + std::to_string(id), desc.timeouts));
      seats_.push_back(std::move(seat));
    }
  }

  [[nodiscard]] std::vector<Seat>& seats() { return seats_; }
  [[nodiscard]] Seat& seat(PlayerId id) { return seats_.at(id - 1); }
  [[nodiscard]] CoopLink& host(PlayerId id) { return *seat(id).host; }
  [[nodiscard]] CoopLink& guest(PlayerId id) { return *seat(id).guest; }

  void pump(int steps = 8, net::Millis step = net::Millis{1}) {
    for (int i = 0; i < steps; ++i) {
      for (Seat& seat : seats_) {
        seat.network->advance(step);
        seat.host->poll(seat.network->now());
        seat.guest->poll(seat.network->now());
      }
    }
  }

  bool connect(int limit = 64) {
    for (int i = 0; i < limit && !ready(); ++i) {
      pump(1);
    }
    return ready();
  }

  [[nodiscard]] bool ready() const {
    return std::ranges::all_of(seats_, [](const Seat& seat) {
      return seat.host->phase() == LinkPhase::Ready && seat.guest->phase() == LinkPhase::Ready;
    });
  }

  // The host sends `message` to every guest; whether every link took it.
  bool sendAll(const Message& message) {
    bool sent = true;
    for (Seat& seat : seats_) {
      sent = seat.host->send(message).has_value() && sent;
    }
    return sent;
  }

  // Nothing travels to or from this guest from now on.
  void cut(PlayerId id) { seat(id).network->cut(); }

private:
  std::vector<Seat> seats_;
};

} // namespace dwe::game::coop::test
