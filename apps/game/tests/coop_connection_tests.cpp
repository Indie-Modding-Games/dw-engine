#include <dwe/net/loopback.hpp>

#include <doctest/doctest.h>

#include "coop_connection.hpp"
#include "coop_gameplay_bridge.hpp"

using namespace dwe::app;

namespace {
class BorrowedPeer final : public dwe::net::PeerLink {
public:
  explicit BorrowedPeer(dwe::net::PeerLink& peer) : peer_{&peer} {}
  void send(dwe::net::Lane lane, std::span<const std::uint8_t> bytes) override {
    peer_->send(lane, bytes);
  }
  std::optional<dwe::net::Bytes> receive(dwe::net::Lane lane) override {
    return peer_->receive(lane);
  }
  void poll(dwe::net::Millis now) override { peer_->poll(now); }
  dwe::net::PeerState state() const override { return peer_->state(); }
  dwe::net::PeerFailure failure() const override { return peer_->failure(); }
  std::optional<dwe::net::Millis> lastHeard() const override { return peer_->lastHeard(); }
  std::string roomCode() const override { return peer_->roomCode(); }

private:
  dwe::net::PeerLink* peer_;
};

dwe::game::coop::Identity identity() {
  return {.protocol = dwe::game::coop::ProtocolVersion,
          .gameVersion = "test",
          .contentHash = "content"};
}
} // namespace

TEST_CASE("cooperative connection host and guest complete the room handshake") {
  dwe::net::LoopbackNetwork network{{}};
  CoopConnection host{
      {.identity = identity(),
       .configurationError = {},
       .host = [&](bool) { return std::make_unique<BorrowedPeer>(network.link(0)); },
       .join = {}}};
  CoopConnection guest{{.identity = identity(),
                        .configurationError = {},
                        .host = {},
                        .join = [&](std::string_view code) {
                          CHECK(code == "TEST");
                          return std::make_unique<BorrowedPeer>(network.link(1));
                        }}};
  host.open();
  guest.open();
  REQUIRE(host.host());
  CHECK(host.code() == "TEST");
  guest.beginJoin();
  guest.type("t-e", false);
  guest.type("st", true);
  CHECK(guest.view() == CoopConnection::View::Joining);

  for (std::uint32_t tick = 0; tick < 20 && (!host.connected() || !guest.connected()); ++tick) {
    network.advance(dwe::net::Millis{10});
    host.tick(network.now());
    guest.tick(network.now());
  }
  CHECK(host.view() == CoopConnection::View::Connected);
  CHECK(guest.view() == CoopConnection::View::Connected);
  REQUIRE(host.link() != nullptr);
  CHECK(host.link()->remotePlayer() == dwe::game::coop::GuestPlayerId);
  host.close();
  guest.close();
}

TEST_CASE("cooperative connection validates the entered room code") {
  CoopConnection connection{
      {.identity = identity(), .configurationError = {}, .host = {}, .join = [](std::string_view) {
         return std::unique_ptr<dwe::net::PeerLink>{};
       }}};
  connection.open();
  connection.beginJoin();
  connection.type("abc", true);
  CHECK(connection.view() == CoopConnection::View::JoinEntry);
  CHECK(connection.error() == "bad_code");
}

TEST_CASE("the lobby leaves the link to the gameplay bridge once play owns it") {
  // Remote player and partner drawn as bare shadows: CoopScreen::tick drained the link every frame,
  // also during play, so the guest never saw a host snapshot and the host never saw a guest
  // command.
  using namespace dwe::game::coop;
  dwe::net::LoopbackNetwork network{{}};
  CoopConnection hostRoom{
      {.identity = identity(),
       .configurationError = {},
       .host = [&](bool) { return std::make_unique<BorrowedPeer>(network.link(0)); },
       .join = {}}};
  CoopConnection guestRoom{
      {.identity = identity(), .configurationError = {}, .host = {}, .join = [&](std::string_view) {
         return std::make_unique<BorrowedPeer>(network.link(1));
       }}};
  hostRoom.open();
  guestRoom.open();
  REQUIRE(hostRoom.host());
  guestRoom.beginJoin();
  guestRoom.type("TEST", true);
  for (std::uint32_t tick = 0; tick < 20 && (!hostRoom.connected() || !guestRoom.connected());
       ++tick) {
    network.advance(dwe::net::Millis{10});
    hostRoom.tick(network.now());
    guestRoom.tick(network.now());
  }
  REQUIRE(hostRoom.connected());
  REQUIRE(guestRoom.connected());
  REQUIRE(hostRoom.link() != nullptr);
  REQUIRE(guestRoom.link() != nullptr);
  CHECK_FALSE(hostRoom.linkInPlay());

  NetworkFieldController guestOnHost;
  NetworkFieldController unused;
  CoopGameplayBridge host{*hostRoom.link(), guestOnHost};
  CoopGameplayBridge guest{*guestRoom.link(), unused};
  hostRoom.handLinkToPlay();
  guestRoom.handLinkToPlay();
  // One game-loop frame on both peers, in CoopScreen::tick's order: the connection ticks, the
  // lobby reads the link unless play owns it, then the bridge polls.
  const auto frame = [&] {
    network.advance(dwe::net::Millis{50});
    for (CoopConnection* room : {&hostRoom, &guestRoom}) {
      room->tick(network.now());
      if (!room->linkInPlay()) {
        while (room->link()->receive()) {
        }
      }
    }
    host.poll(network.now());
    guest.poll(network.now());
  };

  PlayerCommands walk;
  walk.sequence = 7;
  walk.field.stick = {0.0F, -1.0F};
  REQUIRE(guest.sendLocalField(walk));
  const Snapshot snapshot{
      .mapId = "field/main",
      .worldTick = 90,
      .acknowledgedCommand = 7,
      .entities = {{.networkId = 1, .owner = HostPlayerId, .role = EntityRole::Player, .x = 10},
                   {.networkId = 2, .owner = HostPlayerId, .role = EntityRole::Partner, .x = 20}}};
  REQUIRE(host.sendHostSnapshot(snapshot));
  frame();
  CHECK(guestOnHost.next(0).field.stick.y == -1.0F);
  const auto world = guest.presentedWorld(1.0F);
  REQUIRE(world);
  CHECK(world->entities.size() == 2);

  // Another room means another link: the bridge must drop the old one and the lobby reads again.
  const std::uint64_t played = hostRoom.linkGeneration();
  hostRoom.close();
  CHECK(hostRoom.linkGeneration() != played);
  CHECK_FALSE(hostRoom.linkInPlay());
  hostRoom.open();
  REQUIRE(hostRoom.host());
  CHECK(hostRoom.linkGeneration() != played);
  CHECK_FALSE(hostRoom.linkInPlay());
  guestRoom.close();
}
