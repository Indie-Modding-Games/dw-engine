#include <dwe/net/lockstep.hpp>
#include <dwe/net/loopback.hpp>
#include <dwe/net/reliable_channel.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>

using namespace dwe::net;

namespace {

constexpr Millis Frame{50};
constexpr std::uint32_t Delay = 3;

// A toy deterministic simulation: its state mixes in both inputs each tick.
struct Toy {
  std::uint64_t state = 1;
  void step(std::array<std::uint32_t, 2> inputs) {
    state = (state * 6364136223846793005ULL) + inputs[0] + (std::uint64_t{inputs[1]} << 32U) + 1;
  }
};

struct Peer {
  Peer(DatagramLink& link, std::size_t side) : channel{link}, lockstep{side, Delay} {}
  ReliableChannel channel;
  Lockstep lockstep;
  Toy toy;
  std::uint32_t played = 0;
};

// One frame of a peer, as the game runs it: its input, what arrived, the ticks it can play.
void frame(Peer& peer, std::uint32_t input, Millis now,
           const std::function<std::uint64_t(Peer&)>& checksum) {
  if (peer.lockstep.wantsInput()) {
    REQUIRE(peer.channel.send(peer.lockstep.submit(input)));
  }
  peer.channel.poll(now);
  while (auto message = peer.channel.receive()) {
    const auto taken = peer.lockstep.receive(*message);
    REQUIRE(taken);
    CHECK(*taken);
  }
  while (auto inputs = peer.lockstep.ready()) {
    peer.toy.step(*inputs);
    ++peer.played;
    REQUIRE(peer.channel.send(peer.lockstep.played(checksum(peer))));
  }
}

std::uint64_t honest(Peer& peer) {
  return peer.toy.state;
}

} // namespace

TEST_CASE("two peers in lockstep play the same ticks over a bad link") {
  LoopbackNetwork network{
      {.latency = Millis{40}, .jitter = Millis{60}, .lossPercent = 15, .duplicatePercent = 5}, 9};
  Peer host{network.peer(0), 0};
  Peer guest{network.peer(1), 1};
  for (std::uint32_t round = 0; round < 600; ++round) {
    frame(host, round % 7, network.now(), honest);
    frame(guest, (round * 3) % 5, network.now(), honest);
    network.advance(Frame);
  }
  CHECK(host.played > 400);
  CHECK(guest.played > 400);
  // Each stops where it waits for the other's input, at most the delay apart.
  const std::uint32_t both = std::min(host.played, guest.played);
  CHECK(std::max(host.played, guest.played) - both <= Delay + 1);
  CHECK_FALSE(host.lockstep.desyncAt());
  CHECK_FALSE(guest.lockstep.desyncAt());
}

TEST_CASE("the first ticks have no input and each tick waits for both inputs") {
  Lockstep lockstep{0, Delay};
  for (std::uint32_t tick = 0; tick < Delay; ++tick) {
    const auto inputs = lockstep.ready();
    REQUIRE(inputs);
    CHECK(*inputs == std::array<std::uint32_t, 2>{0, 0});
    (void)lockstep.played(tick);
  }
  CHECK_FALSE(lockstep.ready());
  REQUIRE(lockstep.wantsInput());
  (void)lockstep.submit(5);
  CHECK_FALSE(lockstep.ready());
  Lockstep other{1, Delay};
  const Bytes remote = other.submit(9);
  REQUIRE(lockstep.receive(remote));
  const auto inputs = lockstep.ready();
  REQUIRE(inputs);
  CHECK(*inputs == std::array<std::uint32_t, 2>{5, 9});
}

TEST_CASE("a checksum that differs is a desync at its tick, and the run stops there") {
  LoopbackNetwork network{{.latency = Millis{30}}, 2};
  Peer host{network.peer(0), 0};
  Peer guest{network.peer(1), 1};
  const auto drifting = [](Peer& peer) {
    return peer.played >= 50 ? peer.toy.state + 1 : peer.toy.state;
  };
  for (std::uint32_t round = 0; round < 200; ++round) {
    frame(host, 1, network.now(), honest);
    frame(guest, 2, network.now(), drifting);
    network.advance(Frame);
  }
  REQUIRE(host.lockstep.desyncAt());
  CHECK(*host.lockstep.desyncAt() == 49);
  REQUIRE(guest.lockstep.desyncAt());
  CHECK(*guest.lockstep.desyncAt() == 49);
  CHECK(host.played < 60);
}

TEST_CASE("the lockstep leaves other messages alone and refuses damaged ones") {
  Lockstep lockstep{0, Delay};
  const Bytes other{0x10, 1, 2};
  const auto ignored = lockstep.receive(other);
  REQUIRE(ignored);
  CHECK_FALSE(*ignored);
  const Bytes damaged{LockstepInputMessage, 1};
  CHECK_FALSE(lockstep.receive(damaged));
}
