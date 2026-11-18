#include <dwe/net/loopback.hpp>

#include <doctest/doctest.h>

#include "coop_connection.hpp"

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
  CoopConnection host{{.identity = identity(),
                       .configurationError = {},
                       .host = [&] { return std::make_unique<BorrowedPeer>(network.link(0)); },
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
