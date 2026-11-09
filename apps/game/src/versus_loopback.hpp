#pragma once

#include <dwe/net/loopback.hpp>
#include <dwe/net/peer_link.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "versus_flow.hpp"

namespace dwe::app {

// The versus against a second player in this process (--versus-loopback): both flows' links are
// the two ends of a simulated network (net::LoopbackNetwork) with its own clock, and the other
// player joins the room this one hosts, brings the first save that has enough partners and picks
// its first partners, then gives no commands: its fighters act on their own. A developer's way to
// play and look at the versus with one machine; the battles run in lockstep as online.
class VersusLoopback {
public:
  // `rival` is the other player's flow desc without its links and seed, which this sets.
  explicit VersusLoopback(VersusFlowDesc rival);
  ~VersusLoopback();
  VersusLoopback(const VersusLoopback&) = delete;
  VersusLoopback& operator=(const VersusLoopback&) = delete;
  VersusLoopback(VersusLoopback&&) = delete;
  VersusLoopback& operator=(VersusLoopback&&) = delete;

  // This machine's link to the room: it hosts it (the other player's end joins it).
  [[nodiscard]] std::unique_ptr<net::PeerLink> host();
  // Joining is only for the other player: this machine always hosts.
  [[nodiscard]] static std::unique_ptr<net::PeerLink> join(std::string_view code);
  // The network's clock, which both flows tick with.
  [[nodiscard]] net::Millis now() const { return network_.now(); }
  // Once per game tick, after this machine's flow ticked at now(): the other player's flow ticks
  // and acts, and the clock moves on by `elapsed`.
  void tick(net::Millis elapsed);
  // The other player's view of the battles, once it is built (VersusFlow::attachView).
  void attachView(VersusBattleView* view) { rival_.attachView(view); }

private:
  void act();

  net::LoopbackNetwork network_;
  VersusFlow rival_;
  // Whether this machine made its room, and the other player went to join it.
  bool hosting_ = false;
  bool joined_ = false;
};

} // namespace dwe::app
