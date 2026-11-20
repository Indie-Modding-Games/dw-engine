#include <dwe/net/channel_peer_link.hpp>
#include <dwe/net/loopback.hpp>
#include <dwe/net/rtc_peer_link.hpp>
#include <dwe/net/signal.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "fake_signal_server.hpp"

using namespace dwe::net;
using dwe::net::test::FakeSignalServer;

namespace {

SocketOpener openerOf(FakeSignalServer& server) {
  return [&server](const std::string& url) { return server.open(url); };
}

} // namespace

TEST_CASE("a typed room code is read without case, spaces or dashes") {
  CHECK(normalizedRoomCode("k7q2") == "K7Q2");
  CHECK(normalizedRoomCode(" K7-Q2 ") == "K7Q2");
  CHECK_FALSE(normalizedRoomCode("K7Q"));
  CHECK_FALSE(normalizedRoomCode("K7Q22"));
  // 0, 1, O and I are not in the codes: they look alike.
  CHECK_FALSE(normalizedRoomCode("K0Q2"));
  CHECK_FALSE(normalizedRoomCode("KIQ2"));
  CHECK(hostUrl("wss://signal.example/") == "wss://signal.example/v1/host");
  CHECK(hostUrl("wss://signal.example", true) == "wss://signal.example/v1/host?public=1");
  CHECK(joinUrl("wss://signal.example", "K7Q2") == "wss://signal.example/v1/join/K7Q2");
  CHECK(roomsUrl("wss://signal.example/") == "wss://signal.example/v1/rooms");
}

TEST_CASE("the public list holds the listed rooms only, read in one snapshot") {
  FakeSignalServer server;
  SignalClient listed{server.open(hostUrl("wss://fake", true))};
  SignalClient unlisted{server.open(hostUrl("wss://fake"))};
  listed.poll();
  unlisted.poll();
  RoomListRequest request{server.open(roomsUrl("wss://fake"))};
  request.poll();
  REQUIRE(request.done());
  CHECK_FALSE(request.failed());
  REQUIRE(request.rooms().has_value());
  REQUIRE(request.rooms()->size() == 1);
  const PublicRoom& room = request.rooms()->front();
  CHECK(room.joinCode == listed.roomCode());
  CHECK(room.roomId == listed.roomCode());
  CHECK(room.players == 1);
  CHECK(room.capacity == 2);
  CHECK(room.state == "waiting");
  // A server that cannot be reached fails the read instead of waiting for ever.
  server.reachable = false;
  RoomListRequest away{server.open(roomsUrl("wss://fake"))};
  away.poll();
  CHECK(away.failed());
}

TEST_CASE("a snapshot of the public list skips what cannot be a room") {
  const auto rooms = parseRoomList(
      R"({"rooms":[{"roomId":"a","joinCode":"k7q2","players":1,"capacity":4,"state":"waiting"},)"
      R"({"joinCode":"nope"},7]})");
  REQUIRE(rooms.has_value());
  REQUIRE(rooms->size() == 1);
  CHECK(rooms->front().joinCode == "K7Q2");
  CHECK(rooms->front().capacity == 4);
  CHECK_FALSE(parseRoomList("not json"));
  CHECK_FALSE(parseRoomList(R"({"rooms":3})"));
}

TEST_CASE("the signaling client makes a room, pairs and relays both ways") {
  FakeSignalServer server;
  SignalClient host{server.open(hostUrl("wss://fake"))};
  host.poll();
  REQUIRE(host.roomCode() == "K7Q2");
  CHECK_FALSE(host.paired());
  SignalClient guest{server.open(joinUrl("wss://fake", host.roomCode()))};
  guest.poll();
  host.poll();
  CHECK(host.paired());
  CHECK(guest.paired());
  host.relay(R"({"description":"offer sdp","type":"offer"})");
  guest.poll();
  CHECK(guest.receive() == R"({"description":"offer sdp","type":"offer"})");
  guest.relay(R"({"candidate":"a=candidate","mid":"0"})");
  host.poll();
  CHECK(host.receive() == R"({"candidate":"a=candidate","mid":"0"})");
  host.finish();
  guest.finish();
  host.poll();
  guest.poll();
  CHECK(server.finishedRooms == 1);
  CHECK_FALSE(host.failure());
  CHECK_FALSE(guest.failure());
}

TEST_CASE("a wrong code, a full room and a server out of reach fail with their reason") {
  FakeSignalServer server;
  SignalClient wrong{server.open(joinUrl("wss://fake", "ZZZZ"))};
  wrong.poll();
  CHECK(wrong.failure() == PeerFailure::WrongCode);

  SignalClient host{server.open(hostUrl("wss://fake"))};
  host.poll();
  SignalClient first{server.open(joinUrl("wss://fake", host.roomCode()))};
  SignalClient second{server.open(joinUrl("wss://fake", host.roomCode()))};
  second.poll();
  CHECK(second.failure() == PeerFailure::RoomFull);

  server.reachable = false;
  SignalClient away{server.open(hostUrl("wss://fake"))};
  away.poll();
  CHECK(away.failure() == PeerFailure::ServerUnreachable);
}

TEST_CASE("a guest that leaves the room before the route opens closes it for the host") {
  FakeSignalServer server;
  SignalClient host{server.open(hostUrl("wss://fake"))};
  host.poll();
  {
    SignalClient guest{server.open(joinUrl("wss://fake", host.roomCode()))};
    guest.poll();
  }
  host.poll();
  CHECK(host.failure() == PeerFailure::Closed);
}

TEST_CASE("a WebRTC link without a signaling server fails at once") {
  FakeSignalServer server;
  auto link = RtcPeerLink::host({.signalServer = "", .iceServers = {}}, openerOf(server));
  CHECK(link->state() == PeerState::Failed);
  CHECK(link->failure() == PeerFailure::NoServer);
}

TEST_CASE("a WebRTC link reports a wrong code") {
  FakeSignalServer server;
  auto link =
      RtcPeerLink::join({.signalServer = "wss://fake", .iceServers = {}}, "ZZZZ", openerOf(server));
  link->poll(Millis{0});
  CHECK(link->state() == PeerState::Failed);
  CHECK(link->failure() == PeerFailure::WrongCode);
}

TEST_CASE("two WebRTC peers connect through the signaling server and talk on both lanes") {
  // Real libdatachannel peers in one process: host candidates only (no STUN), so no network
  // beyond this machine's own interfaces is needed.
  FakeSignalServer server;
  const RtcSettings settings{.signalServer = "wss://fake", .iceServers = {}};
  auto host = RtcPeerLink::host(settings, openerOf(server));
  host->poll(Millis{0});
  REQUIRE(host->roomCode() == "K7Q2");
  auto guest = RtcPeerLink::join(settings, host->roomCode(), openerOf(server));
  const std::array<std::uint8_t, 3> hello{1, 2, 3};
  // Sent before the route opens: the reliable lane keeps it until then.
  guest->send(Lane::Reliable, hello);
  const auto start = std::chrono::steady_clock::now();
  const auto now = [&] {
    return std::chrono::duration_cast<Millis>(std::chrono::steady_clock::now() - start);
  };
  while ((host->state() != PeerState::Open || guest->state() != PeerState::Open) &&
         host->state() != PeerState::Failed && guest->state() != PeerState::Failed &&
         now() < Millis{20000}) {
    host->poll(now());
    guest->poll(now());
    std::this_thread::sleep_for(Millis{5});
  }
  REQUIRE(host->state() == PeerState::Open);
  REQUIRE(guest->state() == PeerState::Open);
  CHECK(server.finishedRooms == 1);
  const std::array<std::uint8_t, 2> input{0x40, 9};
  std::optional<Bytes> reliable;
  std::optional<Bytes> unreliable;
  const auto deadline = now() + Millis{5000};
  while ((!reliable || !unreliable) && now() < deadline) {
    host->send(Lane::Unreliable, input);
    host->poll(now());
    guest->poll(now());
    if (!reliable) {
      reliable = host->receive(Lane::Reliable);
    }
    if (!unreliable) {
      unreliable = guest->receive(Lane::Unreliable);
    }
    std::this_thread::sleep_for(Millis{5});
  }
  REQUIRE(reliable);
  CHECK(*reliable == Bytes{1, 2, 3});
  REQUIRE(unreliable);
  CHECK(*unreliable == Bytes{0x40, 9});
  CHECK(host->lastHeard());
}

TEST_CASE(
    "a room of more than two seats asks for them, and its host's next seat carries the token") {
  CHECK(hostUrl("wss://signal.test", false, 4) == "wss://signal.test/v1/host?seats=4");
  CHECK(hostUrl("wss://signal.test", true, 3) == "wss://signal.test/v1/host?public=1&seats=3");
  CHECK(hostUrl("wss://signal.test", false, 2) == "wss://signal.test/v1/host");
  CHECK(hostUrl("wss://signal.test", PublicHost{.name = "Mika", .icon = {}, .compat = {}}, 4) ==
        "wss://signal.test/v1/host?public=1&seats=4&name=Mika&mode=campaign");
  CHECK(seatUrl("wss://signal.test/", "K7Q2", "a b", 2) ==
        "wss://signal.test/v1/host/K7Q2?token=a%20b&taken=2");
}

TEST_CASE("a host seats two WebRTC guests in one room, each on a link of its own") {
  FakeSignalServer server;
  RtcSettings settings{.signalServer = "wss://fake", .iceServers = {}};
  settings.seats = 3;
  auto first = RtcPeerLink::host(settings, openerOf(server));
  first->poll(Millis{0});
  REQUIRE(first->roomCode() == "K7Q2");
  REQUIRE(first->roomToken() == "token-K7Q2");
  auto one = RtcPeerLink::join(settings, "K7Q2", openerOf(server));
  const auto start = std::chrono::steady_clock::now();
  const auto now = [&] {
    return std::chrono::duration_cast<Millis>(std::chrono::steady_clock::now() - start);
  };
  const auto connect = [&](PeerLink& host, PeerLink& guest) {
    while ((host.state() != PeerState::Open || guest.state() != PeerState::Open) &&
           host.state() != PeerState::Failed && guest.state() != PeerState::Failed &&
           now() < Millis{20000}) {
      host.poll(now());
      guest.poll(now());
      std::this_thread::sleep_for(Millis{5});
    }
  };
  connect(*first, *one);
  REQUIRE(first->state() == PeerState::Open);
  // The next seat waits for the room's next guest and says how many the host holds.
  auto second =
      RtcPeerLink::hostSeat(settings, first->roomCode(), first->roomToken(), 1, openerOf(server));
  second->poll(now());
  CHECK(second->roomCode() == "K7Q2");
  second->announceSeats({.taken = 1, .playing = false, .joinable = true});
  auto two = RtcPeerLink::join(settings, "K7Q2", openerOf(server));
  connect(*second, *two);
  REQUIRE(second->state() == PeerState::Open);
  REQUIRE(two->state() == PeerState::Open);
  CHECK(server.seatsTaken == std::vector<int>{1, 1});
  // Each guest hears only its own link.
  const std::array<std::uint8_t, 1> toOne{1};
  const std::array<std::uint8_t, 1> toTwo{2};
  first->send(Lane::Reliable, toOne);
  second->send(Lane::Reliable, toTwo);
  std::optional<Bytes> heardOne;
  std::optional<Bytes> heardTwo;
  const auto deadline = now() + Millis{5000};
  while ((!heardOne || !heardTwo) && now() < deadline) {
    for (PeerLink* link :
         {static_cast<PeerLink*>(first.get()), static_cast<PeerLink*>(second.get()),
          static_cast<PeerLink*>(one.get()), static_cast<PeerLink*>(two.get())}) {
      link->poll(now());
    }
    heardOne = heardOne ? heardOne : one->receive(Lane::Reliable);
    heardTwo = heardTwo ? heardTwo : two->receive(Lane::Reliable);
    std::this_thread::sleep_for(Millis{5});
  }
  CHECK(heardOne == Bytes{1});
  CHECK(heardTwo == Bytes{2});
  // A third guest finds the room full: no seat waits for it.
  auto third = RtcPeerLink::join(settings, "K7Q2", openerOf(server));
  third->poll(now());
  CHECK(third->failure() == PeerFailure::RoomFull);
  // The game started: the host's next seat says it is under way and whether it takes new players.
  auto playing =
      RtcPeerLink::hostSeat(settings, first->roomCode(), first->roomToken(), 2, openerOf(server));
  playing->poll(now());
  playing->announceSeats({.taken = 2, .playing = true, .joinable = false});
  playing->poll(now());
  CHECK(server.gamesUnderway == std::vector<std::string>{"closed"});
}

TEST_CASE("a channel peer link keeps the lanes apart over one datagram link") {
  LoopbackNetwork network{{.latency = Millis{10}, .lossPercent = 30}, 4};
  PeerLink& one = network.link(0);
  PeerLink& two = network.link(1);
  CHECK(one.state() == PeerState::Open);
  CHECK(one.roomCode() == "TEST");
  for (std::uint8_t index = 0; index < 20; ++index) {
    const std::array<std::uint8_t, 1> message{index};
    one.send(Lane::Reliable, message);
  }
  std::vector<std::uint8_t> got;
  for (int step = 0; step < 200; ++step) {
    one.poll(network.now());
    two.poll(network.now());
    while (auto message = two.receive(Lane::Reliable)) {
      got.push_back(message->front());
    }
    network.advance(Millis{20});
  }
  REQUIRE(got.size() == 20);
  for (std::uint8_t index = 0; index < 20; ++index) {
    CHECK(got.at(index) == index);
  }
  const std::array<std::uint8_t, 1> loose{7};
  one.send(Lane::Unreliable, loose);
  CHECK_FALSE(two.receive(Lane::Unreliable));
  CHECK(two.lastHeard());
}
