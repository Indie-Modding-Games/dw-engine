#include <dwe/net/loopback.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <set>

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

// A host's link in a room of more seats: it carries the room's token, as the signaling server's
// room message does.
class SeatedPeer final : public dwe::net::PeerLink {
public:
  explicit SeatedPeer(dwe::net::PeerLink& peer) : peer_{&peer} {}
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
  std::string roomToken() const override { return "token"; }

private:
  dwe::net::PeerLink* peer_;
};

dwe::game::coop::Identity identity() {
  return {.protocol = dwe::game::coop::ProtocolVersion,
          .gameVersion = "test",
          .contentHash = "content"};
}

CoopConnectionDesc roomDesc(
    std::function<std::unique_ptr<dwe::net::PeerLink>(const std::optional<dwe::net::PublicHost>&)>
        host,
    std::function<std::unique_ptr<dwe::net::PeerLink>(std::string_view)> join) {
  return {.identity = identity(),
          .configurationError = {},
          .host = std::move(host),
          .join = std::move(join),
          .seat = {},
          .seats = 2,
          .compat = "coop-test-content",
          .signalServer = {},
          .open = {}};
}
} // namespace

TEST_CASE("cooperative connection host and guest complete the room handshake") {
  dwe::net::LoopbackNetwork network{{}};
  CoopConnection host{roomDesc(
      [&](const std::optional<dwe::net::PublicHost>&) {
        return std::make_unique<BorrowedPeer>(network.link(0));
      },
      {})};
  CoopConnection guest{roomDesc({}, [&](std::string_view code) {
    CHECK(code == "TEST");
    return std::make_unique<BorrowedPeer>(network.link(1));
  })};
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
      roomDesc({}, [](std::string_view) { return std::unique_ptr<dwe::net::PeerLink>{}; })};
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
  CoopConnection hostRoom{roomDesc(
      [&](const std::optional<dwe::net::PublicHost>&) {
        return std::make_unique<BorrowedPeer>(network.link(0));
      },
      {})};
  CoopConnection guestRoom{roomDesc(
      {}, [&](std::string_view) { return std::make_unique<BorrowedPeer>(network.link(1)); })};
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
  const auto world = guest.presentedWorld(network.now());
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

TEST_CASE("the cooperative room's phase follows the link, the lobby and leaving") {
  using dwe::net::RoomPhase;
  dwe::net::LoopbackNetwork network{{}};
  std::optional<dwe::net::PublicHost> told;
  CoopConnection host{roomDesc(
      [&](const std::optional<dwe::net::PublicHost>& listing) {
        told = listing;
        return std::make_unique<BorrowedPeer>(network.link(0));
      },
      {})};
  CoopConnection guest{roomDesc(
      {}, [&](std::string_view) { return std::make_unique<BorrowedPeer>(network.link(1)); })};
  host.setPlayerName("  Ana*  ");
  host.setPlayerIcon("crown");
  guest.setPlayerName("Mika");
  CHECK(host.room() == nullptr);
  host.open();
  guest.open();
  REQUIRE(host.host(true));
  // A public room is listed with the host's name as the directory takes it, its icon and key.
  REQUIRE(told.has_value());
  CHECK(told->name == "Ana");
  CHECK(told->icon == "crown");
  CHECK(told->compat == "coop-test-content");
  CHECK(told->mode == "campaign");
  REQUIRE(guest.join("TEST"));
  REQUIRE(guest.room() != nullptr);
  CHECK(guest.room()->phase() == RoomPhase::Connecting);
  CHECK(guest.room()->code() == "TEST");
  CHECK(guest.code() == "TEST");
  for (std::uint32_t tick = 0; tick < 20 && (!host.connected() || !guest.connected()); ++tick) {
    network.advance(dwe::net::Millis{10});
    host.tick(network.now());
    guest.tick(network.now());
  }
  REQUIRE(host.room() != nullptr);
  CHECK(host.room()->hosting());
  CHECK(host.room()->code() == "TEST");
  CHECK(host.room()->phase() == RoomPhase::Connected);
  CHECK(guest.room()->phase() == RoomPhase::Connected);
  // Each knows the other's name: the host's reaches the guest with its welcome.
  CHECK(host.remoteName() == "Mika");
  CHECK(guest.remoteName() == "  Ana*  ");
  host.markRoomReady(true);
  CHECK(host.room()->phase() == RoomPhase::Ready);
  host.markRoomReady(false);
  CHECK(host.room()->phase() == RoomPhase::Connected);
  host.markRoomPlaying();
  CHECK(host.room()->phase() == RoomPhase::Playing);
  // Playing never goes back to ready.
  host.markRoomReady(true);
  CHECK(host.room()->phase() == RoomPhase::Playing);
  host.close();
  CHECK(host.room()->phase() == RoomPhase::Closed);
  CHECK_FALSE(host.room()->noticesPending());
  guest.close();
  CHECK(guest.room()->phase() == RoomPhase::Closed);
}

TEST_CASE("a cooperative room that cannot be made closes with the error") {
  CoopConnection connection{roomDesc(
      [](const std::optional<dwe::net::PublicHost>&) {
        return std::unique_ptr<dwe::net::PeerLink>{};
      },
      {})};
  connection.open();
  CHECK_FALSE(connection.host(false));
  CHECK(connection.view() == CoopConnection::View::Error);
  REQUIRE(connection.room() != nullptr);
  CHECK(connection.room()->phase() == dwe::net::RoomPhase::Closed);
  CHECK(connection.room()->closeReason() == "transport_unavailable");
}

namespace {

// A host with seats for four and three guests, each over a loopback network of its own (the
// signaling server pairs each guest with one of the host's links); the fourth network serves a
// seat the host offers again once a guest left.
struct FourSeats {
  std::array<std::unique_ptr<dwe::net::LoopbackNetwork>, 4> networks;
  std::unique_ptr<CoopConnection> host;
  std::array<std::unique_ptr<CoopConnection>, 3> guests;
  std::vector<std::string> seatsAsked;

  FourSeats() {
    for (auto& network : networks) {
      network = std::make_unique<dwe::net::LoopbackNetwork>(dwe::net::LinkConditions{});
    }
    CoopConnectionDesc desc = roomDesc(
        [this](const std::optional<dwe::net::PublicHost>&) {
          return std::make_unique<SeatedPeer>(networks[0]->link(0));
        },
        {});
    desc.seat = [this](std::string_view code, std::string_view token, int taken) {
      seatsAsked.push_back(std::format("{}/{}/{}", code, token, taken));
      return std::make_unique<SeatedPeer>(
          networks[std::min(seatsAsked.size(), networks.size() - 1)]->link(0));
    };
    desc.seats = 4;
    host = std::make_unique<CoopConnection>(std::move(desc));
    for (std::size_t index = 0; index < guests.size(); ++index) {
      guests[index] =
          std::make_unique<CoopConnection>(roomDesc({}, [this, index](std::string_view) {
            return std::make_unique<BorrowedPeer>(networks[index]->link(1));
          }));
    }
  }

  void run(std::uint32_t ticks) {
    for (std::uint32_t tick = 0; tick < ticks; ++tick) {
      for (auto& network : networks) {
        network->advance(dwe::net::Millis{10});
      }
      const dwe::net::Millis now = networks[0]->now();
      host->tick(now);
      for (auto& guest : guests) {
        guest->tick(now);
      }
    }
  }
};

} // namespace

TEST_CASE("a host seats three guests in one room, each with its own player id (#352)") {
  FourSeats room;
  room.host->open();
  REQUIRE(room.host->host());
  CHECK(room.host->seats() == 4);
  for (auto& guest : room.guests) {
    guest->open();
    REQUIRE(guest->join("TEST"));
  }
  room.run(30);
  CHECK(room.host->view() == CoopConnection::View::Connected);
  const auto in = room.host->guests();
  REQUIRE(in.size() == 3);
  for (std::size_t index = 0; index < in.size(); ++index) {
    CHECK(in[index].id == index + 1);
  }
  std::set<dwe::game::coop::PlayerId> given;
  for (auto& guest : room.guests) {
    REQUIRE(guest->connected());
    given.insert(guest->link()->localPlayer());
  }
  CHECK(given == std::set<dwe::game::coop::PlayerId>{1, 2, 3});
  // The host asked the server for its next seats with the room's code and token.
  REQUIRE(room.seatsAsked.size() == 2);
  CHECK(room.seatsAsked.front().starts_with("TEST/token/"));

  // The second guest leaves the lobby: the others stay, the room goes on and offers its seat again.
  const dwe::game::coop::PlayerId leaving = room.guests[1]->link()->localPlayer();
  room.guests[1]->close();
  room.run(5);
  CHECK(room.host->view() == CoopConnection::View::Connected);
  const auto left = room.host->guests();
  REQUIRE(left.size() == 2);
  CHECK(std::ranges::none_of(
      left, [&](const CoopConnection::Guest& guest) { return guest.id == leaving; }));
  CHECK(room.guests[0]->connected());
  CHECK(room.guests[2]->connected());
  REQUIRE(room.seatsAsked.size() == 3);
  CHECK(room.seatsAsked.back() == "TEST/token/2");
  room.host->close();
}

TEST_CASE("a started room of four ends once its last guest went, like a room of two") {
  FourSeats room;
  room.host->open();
  REQUIRE(room.host->host());
  room.guests[0]->open();
  REQUIRE(room.guests[0]->join("TEST"));
  room.guests[1]->open();
  REQUIRE(room.guests[1]->join("TEST"));
  room.run(30);
  REQUIRE(room.host->guests().size() == 2);
  room.host->markRoomPlaying();
  room.host->handLinkToPlay();
  for (const CoopConnection::Guest& guest : room.host->guests()) {
    CHECK(room.host->linkInPlay(guest.generation));
  }
  room.guests[0]->close();
  room.run(5);
  // One guest still plays: the host's game goes on with it.
  CHECK(room.host->view() == CoopConnection::View::Connected);
  REQUIRE(room.host->guests().size() == 1);
  room.guests[1]->close();
  room.run(5);
  CHECK(room.host->view() == CoopConnection::View::Error);
  room.host->close();
}
