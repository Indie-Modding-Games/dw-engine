#include <dwe/net/room_session.hpp>
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

// A link whose room, peer and failure the test sets.
class FakeLink final : public PeerLink {
public:
  void send(Lane /*lane*/, std::span<const std::uint8_t> /*message*/) override {}
  std::optional<Bytes> receive(Lane /*lane*/) override { return std::nullopt; }
  void poll(Millis /*now*/) override {}
  [[nodiscard]] PeerState state() const override { return state_; }
  [[nodiscard]] PeerFailure failure() const override { return failure_; }
  [[nodiscard]] std::optional<Millis> lastHeard() const override { return std::nullopt; }
  [[nodiscard]] std::string roomCode() const override { return code; }
  [[nodiscard]] bool peerFound() const override { return found; }
  [[nodiscard]] std::string roomToken() const override { return token; }

  void failWith(PeerFailure why) {
    state_ = PeerState::Failed;
    failure_ = why;
  }

  std::string code;
  std::string token;
  bool found = false;

private:
  PeerState state_ = PeerState::Connecting;
  PeerFailure failure_ = PeerFailure::None;
};

const nlohmann::json Relays = nlohmann::json::parse(
    R"([{"urls":["turn:127.0.0.1:3478?transport=udp","turns:127.0.0.1:5349?transport=tcp"],)"
    R"("username":"user","credential":"pa:ss/w+rd="}])");

} // namespace

TEST_CASE("a public room's host tells the directory its name, icon, compatibility key and mode") {
  const PublicHost listing{
      .name = "  José  María!<>", .icon = "egg", .compat = "coop-2-ab", .mode = "campaign"};
  CHECK(
      hostUrl("wss://s/", listing) ==
      "wss://s/v1/host?public=1&name=Jos%C3%A9%20Mar%C3%ADa&icon=egg&compat=coop-2-ab&mode=campaign");
  CHECK(hostUrl("wss://s", PublicHost{.name = "<>", .icon = {}, .compat = {}, .mode = {}}) ==
        "wss://s/v1/host?public=1");
  CHECK(directoryName("Ana-María O'Neil") == "Ana-María O'Neil");
  CHECK(directoryName("abcdefghijklmnopqrstu") == "abcdefghijklmnop");
  CHECK(directoryName("a×b\xff\n c") == "ab c");
  CHECK(directoryName("😀") == "");
  CHECK(roomsUrl("wss://s", "coop 2") == "wss://s/v1/rooms?compat=coop%202");
  CHECK(infoUrl("wss://s/") == "wss://s/v1/info");
  CHECK(stateUrl("wss://s", "K7Q2", "t0k", "ready") ==
        "wss://s/v1/rooms/K7Q2/state?token=t0k&state=ready");
}

TEST_CASE("a compatibility key keeps the letters the directory takes, at most 64") {
  CHECK(compatKey("coop", "0.1.0-beta.12+g1a2b", "0123456789abcdef0123") ==
        "coop-0.1.0-beta.12-g1a2b-0123456789abcdef");
  CHECK(compatKey("versus", std::string(80, 'v'), "ab").size() == 64);
}

TEST_CASE("relay servers become libdatachannel URLs, UDP only, with their credentials encoded") {
  const std::vector<IceServer> servers{
      {.urls = {"turn:turn.example:3478?transport=udp", "turn:turn.example:80?transport=tcp",
                "turns:turn.example:443?transport=tcp", "turn:turn.example:3478", "stun:x:3478"},
       .username = "user",
       .credential = "pa:ss/w+rd="},
      {.urls = {"turn:nocreds:3478"}, .username = {}, .credential = {}},
  };
  CHECK(relayUrls(servers) ==
        std::vector<std::string>{"turn:user:pa%3Ass%2Fw%2Brd%3D@turn.example:3478?transport=udp",
                                 "turn:user:pa%3Ass%2Fw%2Brd%3D@turn.example:3478"});
}

TEST_CASE("the signaling client keeps a public room's token and the relay servers") {
  FakeSignalServer server;
  server.relays = Relays;
  SignalClient host{server.open(hostUrl(
      "wss://fake", PublicHost{.name = "Mika", .icon = "egg", .compat = "k", .mode = "campaign"}))};
  host.poll();
  CHECK(host.roomToken() == "token-K7Q2");
  REQUIRE(host.iceServers().size() == 1);
  CHECK(host.iceServers().front().credential == "pa:ss/w+rd=");
  CHECK(host.iceServers().front().urls.size() == 2);
  SignalClient guest{server.open(joinUrl("wss://fake", "K7Q2"))};
  guest.poll();
  CHECK(guest.paired());
  CHECK(guest.roomToken().empty());
  CHECK(guest.iceServers() == host.iceServers());

  // A private room has no token; a server without TURN hands out no relays.
  server.relays = nlohmann::json::array();
  SignalClient quiet{server.open(hostUrl("wss://fake"))};
  quiet.poll();
  CHECK(quiet.roomCode() == "M4XP");
  CHECK(quiet.roomToken().empty());
  CHECK(quiet.iceServers().empty());
}

TEST_CASE("a server's limits refuse the room or the list") {
  FakeSignalServer server;
  server.refuseHost = true;
  SignalClient host{server.open(hostUrl("wss://fake", true))};
  host.poll();
  CHECK(host.failure() == PeerFailure::Refused);
  server.refuseList = true;
  RoomListRequest list{server.open(roomsUrl("wss://fake"))};
  list.poll();
  CHECK(list.done());
  CHECK(list.failed());
  CHECK(list.refusal() == "limit");
}

TEST_CASE("the server says what it can do; a first-protocol server says nothing") {
  FakeSignalServer server;
  ServerInfoRequest plain{server.open(infoUrl("wss://fake"))};
  plain.poll();
  REQUIRE(plain.info());
  CHECK(plain.info()->has("public-rooms"));
  CHECK_FALSE(plain.info()->has("relay"));
  server.relays = Relays;
  ServerInfoRequest relayed{server.open(infoUrl("wss://fake"))};
  relayed.poll();
  REQUIRE(relayed.info());
  CHECK(relayed.info()->has("relay"));
  server.knowsInfo = false;
  ServerInfoRequest old{server.open(infoUrl("wss://fake"))};
  old.poll();
  CHECK(old.done());
  CHECK_FALSE(old.info());
  CHECK_FALSE(parseServerInfo(R"({"protocol":"v1"})"));
}

TEST_CASE("the list carries each room's phase, its host's name and its compatibility key") {
  FakeSignalServer server;
  SignalClient mine{server.open(hostUrl(
      "wss://fake", PublicHost{.name = "Mika", .icon = "egg", .compat = "a", .mode = "campaign"}))};
  SignalClient other{server.open(hostUrl(
      "wss://fake", PublicHost{.name = "Ana", .icon = "egg", .compat = "b", .mode = "campaign"}))};
  mine.poll();
  other.poll();
  RoomListRequest list{server.open(roomsUrl("wss://fake", "a"))};
  list.poll();
  REQUIRE(list.rooms());
  REQUIRE(list.rooms()->size() == 1);
  CHECK(list.rooms()->front().hostName == "Mika");
  CHECK(list.rooms()->front().phase == "open");
  CHECK(list.rooms()->front().compat == "a");
}

TEST_CASE(
    "a public room's session goes open, connected, ready, playing and closed, telling the directory") {
  FakeSignalServer server;
  SignalClient signal{server.open(hostUrl("wss://fake", true))};
  signal.poll();
  FakeLink link;
  RoomSession room{{.hosting = true, .code = {}, .server = "wss://fake", .open = openerOf(server)}};
  CHECK(room.phase() == RoomPhase::Connecting);
  room.observe(link);
  CHECK(room.phase() == RoomPhase::Connecting);
  link.code = signal.roomCode();
  link.token = signal.roomToken();
  room.observe(link);
  CHECK(room.phase() == RoomPhase::Open);
  CHECK(room.code() == "K7Q2");
  CHECK(room.listed());
  // Ready and playing come after a guest: ignored while open.
  room.markReady(true);
  room.markPlaying();
  CHECK(room.phase() == RoomPhase::Open);
  CHECK(server.notices.empty());

  SignalClient guest{server.open(joinUrl("wss://fake", "K7Q2"))};
  link.found = true;
  room.observe(link);
  CHECK(room.phase() == RoomPhase::Connected);
  CHECK(server.phaseOf("K7Q2") == "connected");
  room.markReady(true);
  CHECK(room.phase() == RoomPhase::Ready);
  CHECK(server.phaseOf("K7Q2") == "ready");
  room.markReady(false);
  CHECK(room.phase() == RoomPhase::Connected);
  room.markPlaying();
  CHECK(room.phase() == RoomPhase::Playing);
  CHECK(server.phaseOf("K7Q2") == "playing");
  // Playing cannot go back to ready.
  room.markReady(true);
  CHECK(room.phase() == RoomPhase::Playing);
  room.close();
  CHECK(room.phase() == RoomPhase::Closed);
  CHECK(server.notices ==
        std::vector<std::string>{"K7Q2 ready", "K7Q2 connected", "K7Q2 playing", "K7Q2 closed"});
  CHECK(room.sentNotices() == std::deque<std::string>{"ready", "connected", "playing", "closed"});
  CHECK(room.noticeError().empty());
  CHECK_FALSE(room.noticesPending());
  // Closed is the end: nothing moves it again.
  link.found = true;
  room.observe(link);
  room.markPlaying();
  CHECK(room.phase() == RoomPhase::Closed);
}

TEST_CASE(
    "a room session's errors: a failed link closes it, a refused notice is kept, a private room tells nothing") {
  FakeSignalServer server;
  FakeLink wrong;
  RoomSession guest{
      {.hosting = false, .code = "ZZZZ", .server = "wss://fake", .open = openerOf(server)}};
  CHECK(guest.code() == "ZZZZ");
  wrong.failWith(PeerFailure::WrongCode);
  guest.observe(wrong);
  CHECK(guest.phase() == RoomPhase::Closed);
  CHECK(guest.failure() == PeerFailure::WrongCode);

  // A host whose token the directory does not know: the room goes on, the error is kept.
  SignalClient signal{server.open(hostUrl("wss://fake", true))};
  signal.poll();
  FakeLink link;
  link.code = signal.roomCode();
  link.token = "stolen";
  link.found = true;
  RoomSession forged{
      {.hosting = true, .code = {}, .server = "wss://fake", .open = openerOf(server)}};
  forged.observe(link);
  forged.markReady(true);
  CHECK(forged.phase() == RoomPhase::Ready);
  CHECK(forged.noticeError() == "bad-token");
  CHECK(server.notices.empty());

  // An unreachable server: the notice is lost, the room goes on.
  server.reachable = false;
  forged.markPlaying();
  CHECK(forged.phase() == RoomPhase::Playing);
  CHECK(forged.noticeError() == "unreachable");
  server.reachable = true;

  // A private room has no token: its phases are its own.
  SignalClient privately{server.open(hostUrl("wss://fake"))};
  privately.poll();
  FakeLink quiet;
  quiet.code = privately.roomCode();
  quiet.found = true;
  const std::size_t opened = server.opened.size();
  RoomSession room{{.hosting = true, .code = {}, .server = "wss://fake", .open = openerOf(server)}};
  room.observe(quiet);
  room.markReady(true);
  room.markPlaying();
  room.close("left");
  CHECK(room.phase() == RoomPhase::Closed);
  CHECK(room.closeReason() == "left");
  CHECK_FALSE(room.listed());
  CHECK(server.opened.size() == opened);

  // A public room closed while open says nothing: its room leaves the list by itself.
  SignalClient lonely{server.open(hostUrl("wss://fake", true))};
  lonely.poll();
  FakeLink open;
  open.code = lonely.roomCode();
  open.token = lonely.roomToken();
  RoomSession waiting{
      {.hosting = true, .code = {}, .server = "wss://fake", .open = openerOf(server)}};
  waiting.observe(open);
  waiting.close();
  CHECK(waiting.sentNotices().empty());
  CHECK(roomPhaseName(RoomPhase::Ready) == "ready");
}

TEST_CASE("a public room's host repeats its state every minute while the room lasts (#416)") {
  FakeSignalServer server;
  SignalClient signal{server.open(hostUrl("wss://fake", true))};
  signal.poll();
  FakeLink link;
  link.code = signal.roomCode();
  link.token = signal.roomToken();
  RoomSession room{{.hosting = true, .code = {}, .server = "wss://fake", .open = openerOf(server)}};
  room.observe(link);
  // Open: the room's own socket keeps its entry, nothing is repeated.
  room.poll(Millis{0});
  room.poll(RoomRefreshEvery * 3);
  CHECK(server.notices.empty());
  SignalClient guest{server.open(joinUrl("wss://fake", "K7Q2"))};
  link.found = true;
  room.observe(link);
  Millis now = RoomRefreshEvery * 3;
  room.poll(now);
  room.poll(now + RoomRefreshEvery - Millis{1});
  CHECK(server.notices.empty());
  room.poll(now + RoomRefreshEvery);
  CHECK(server.notices == std::vector<std::string>{"K7Q2 connected"});
  room.markPlaying();
  now += RoomRefreshEvery * 2;
  room.poll(now);
  room.poll(now + RoomRefreshEvery);
  room.poll(now + RoomRefreshEvery * 2);
  CHECK(server.notices ==
        std::vector<std::string>{"K7Q2 connected", "K7Q2 playing", "K7Q2 playing", "K7Q2 playing"});
  room.close();
  room.poll(now + RoomRefreshEvery * 5);
  CHECK(server.notices.back() == "K7Q2 closed");
  CHECK(server.notices.size() == 5);

  // A server that predates the «connected» notice refuses it once and hears no more of it.
  FakeSignalServer older;
  older.takesConnected = false;
  SignalClient oldSignal{older.open(hostUrl("wss://fake", true))};
  oldSignal.poll();
  FakeLink oldLink;
  oldLink.code = oldSignal.roomCode();
  oldLink.token = oldSignal.roomToken();
  oldLink.found = true;
  RoomSession oldRoom{
      {.hosting = true, .code = {}, .server = "wss://fake", .open = openerOf(older)}};
  oldRoom.observe(oldLink);
  for (int minute = 0; minute <= 5; ++minute) {
    oldRoom.poll(RoomRefreshEvery * minute);
  }
  CHECK(oldRoom.noticeError() == "bad-state");
  CHECK(older.notices.empty());
  oldRoom.markReady(true);
  oldRoom.poll(RoomRefreshEvery * 6);
  oldRoom.poll(RoomRefreshEvery * 7);
  CHECK(older.notices == std::vector<std::string>{"K7Q2 ready", "K7Q2 ready"});
}

TEST_CASE("relay only fails at once without a relay, and with one it uses no direct route") {
  FakeSignalServer server;
  const RtcSettings settings{.signalServer = "wss://fake",
                             .iceServers = {"stun:127.0.0.1:3478"},
                             .relayOnly = true,
                             .connectTimeout = Millis{300}};
  auto host = RtcPeerLink::host(settings, openerOf(server));
  host->poll(Millis{0});
  auto guest = RtcPeerLink::join(settings, host->roomCode(), openerOf(server));
  guest->poll(Millis{0});
  host->poll(Millis{1});
  CHECK(host->state() == PeerState::Failed);
  CHECK(host->failure() == PeerFailure::NoRelay);
  CHECK(guest->failure() == PeerFailure::NoRelay);

  // With a relay that answers nothing, the two peers on this machine never take their direct
  // route: relay only holds, and the route times out.
  FakeSignalServer relayed;
  relayed.relays = Relays;
  auto relayHost = RtcPeerLink::host(settings, openerOf(relayed));
  relayHost->poll(Millis{0});
  auto relayGuest = RtcPeerLink::join(settings, relayHost->roomCode(), openerOf(relayed));
  const auto start = std::chrono::steady_clock::now();
  const auto now = [&] {
    return std::chrono::duration_cast<Millis>(std::chrono::steady_clock::now() - start);
  };
  while ((relayHost->state() == PeerState::Connecting ||
          relayGuest->state() == PeerState::Connecting) &&
         now() < Millis{10000}) {
    relayHost->poll(now());
    relayGuest->poll(now());
    std::this_thread::sleep_for(Millis{5});
  }
  CHECK(relayHost->state() == PeerState::Failed);
  CHECK(relayHost->failure() != PeerFailure::NoRelay);
  CHECK(relayGuest->state() == PeerState::Failed);
}

TEST_CASE(
    "two WebRTC peers in a public room: the session follows them and the relays do not get in the way") {
  FakeSignalServer server;
  server.relays = Relays;
  const RtcSettings settings{.signalServer = "wss://fake", .iceServers = {}};
  auto host = RtcPeerLink::host(
      settings, openerOf(server),
      PublicHost{.name = "Mika", .icon = "egg", .compat = "k", .mode = "campaign"});
  RoomSession hostRoom{
      {.hosting = true, .code = {}, .server = "wss://fake", .open = openerOf(server)}};
  host->poll(Millis{0});
  hostRoom.observe(*host);
  REQUIRE(hostRoom.phase() == RoomPhase::Open);
  CHECK(host->roomToken() == "token-K7Q2");
  auto guest = RtcPeerLink::join(settings, host->roomCode(), openerOf(server));
  RoomSession guestRoom{{.hosting = false, .code = host->roomCode(), .server = {}, .open = {}}};
  const auto start = std::chrono::steady_clock::now();
  const auto now = [&] {
    return std::chrono::duration_cast<Millis>(std::chrono::steady_clock::now() - start);
  };
  while ((host->state() != PeerState::Open || guest->state() != PeerState::Open) &&
         host->state() != PeerState::Failed && guest->state() != PeerState::Failed &&
         now() < Millis{20000}) {
    host->poll(now());
    guest->poll(now());
    hostRoom.observe(*host);
    guestRoom.observe(*guest);
    std::this_thread::sleep_for(Millis{5});
  }
  REQUIRE(host->state() == PeerState::Open);
  REQUIRE(guest->state() == PeerState::Open);
  CHECK(hostRoom.phase() == RoomPhase::Connected);
  CHECK(guestRoom.phase() == RoomPhase::Connected);
  // The room closed on the server, its entry stays for the host's notices.
  CHECK(server.phaseOf("K7Q2") == "connected");
  hostRoom.markPlaying();
  guestRoom.markPlaying();
  CHECK(server.phaseOf("K7Q2") == "playing");
  hostRoom.close();
  CHECK(server.phaseOf("K7Q2").empty());
}
