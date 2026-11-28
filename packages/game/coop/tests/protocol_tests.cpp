#include <dwe/game/coop/link.hpp>
#include <dwe/game/coop/profile_transfer.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/loopback.hpp>
#include <dwe/net/peer_link.hpp>
#include <dwe/net/wire.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace dwe::game::coop;
namespace net = dwe::net;

namespace {

Identity identity(std::uint32_t version = ProtocolVersion) {
  return {.protocol = version, .gameVersion = "1.2.3", .contentHash = "content-hash"};
}

template <typename T>
void roundTrip(const T& value) {
  const auto bytes = encode(Message{value});
  const auto decoded = decode(bytes);
  REQUIRE(decoded);
  REQUIRE(decoded->has_value());
  CHECK(std::get<T>(**decoded) == value);
}

} // namespace

TEST_CASE("cooperative control and state messages round trip") {
  roundTrip(Hello{.identity = identity(), .playerName = "Guest", .rejoinKey = 0});
  roundTrip(
      Hello{.identity = identity(), .playerName = "Guest", .rejoinKey = 0xABCDEF0123456789ULL});
  roundTrip(Welcome{.identity = identity(),
                    .accepted = true,
                    .assignedPlayer = GuestPlayerId,
                    .reason = RejectReason::None,
                    .sessionId = 0x1020304050607080ULL,
                    .hostName = {},
                    .seatKey = 0});
  roundTrip(Welcome{.identity = identity(),
                    .accepted = false,
                    .assignedPlayer = GuestPlayerId,
                    .reason = RejectReason::RoomFull,
                    .sessionId = 42,
                    .hostName = {},
                    .seatKey = 0});
  roundTrip(Ready{.ready = false});
  roundTrip(Ready{.ready = true});
  roundTrip(JoinState{.formatVersion = 1,
                      .mapId = "city/field",
                      .worldTick = 321,
                      .saveTransferId = 99,
                      .saveChunkCount = 4,
                      .entities = {{.networkId = 4,
                                    .owner = GuestPlayerId,
                                    .role = EntityRole::Partner,
                                    .x = 42,
                                    .y = 0,
                                    .z = -80,
                                    .yaw = 60000,
                                    .motion = Motion::Idle}}});
  roundTrip(Commands{.sequence = 0xF0E0D0C0U,
                     .field = {.stickX = -32767,
                               .stickY = 32767,
                               .run = true,
                               .cancelTarget = false,
                               .confirm = true,
                               .moveTarget = std::pair{-123456, 789012}}});
  roundTrip(Commands{.sequence = 12, .field = {}});
  roundTrip(Snapshot{.mapId = "city/field",
                     .worldTick = 456,
                     .acknowledgedCommand = 78,
                     .entities = {{.networkId = 1,
                                   .owner = HostPlayerId,
                                   .role = EntityRole::Player,
                                   .x = -1000,
                                   .y = 3,
                                   .z = 2000,
                                   .yaw = 65530,
                                   .motion = Motion::Run},
                                  {.networkId = 2,
                                   .owner = GuestPlayerId,
                                   .role = EntityRole::Partner,
                                   .x = 1000,
                                   .y = 0,
                                   .z = -2000,
                                   .yaw = 19,
                                   .motion = Motion::Walk}}});
  roundTrip(ViewState{.player = GuestPlayerId,
                      .sequence = 80,
                      .mapId = "city/field",
                      .left = 100,
                      .top = 200,
                      .right = 1380,
                      .bottom = 920});
  roundTrip(ProfileChunk{.transferId = 88, .index = 0, .count = 1, .bytes = {1, 2, 3, 4}});
  roundTrip(ProfileChunk{.transferId = 89,
                         .index = 0,
                         .count = 1,
                         .kind = ProfilePayloadKind::HostSave,
                         .bytes = {5, 6, 7}});
  roundTrip(Leave{.reason = LeaveReason::Disconnected});
  roundTrip(PlayerState{.player = 3,
                        .sequence = 0xFFFFFFFEU,
                        .epoch = 9,
                        .mapId = "mayo01",
                        .x = -2309,
                        .y = 0,
                        .z = MaxWorldCoordinate,
                        .yaw = 65535,
                        .motion = Motion::Walk});
  roundTrip(PlayerCorrection{.player = 2,
                             .epoch = 4,
                             .mapId = "mayo02",
                             .exit = 1,
                             .x = 10,
                             .y = -5,
                             .z = 20,
                             .yaw = 300});
  roundTrip(PlayerCorrection{
      .player = 1, .epoch = 1, .mapId = "m", .exit = {}, .x = 0, .y = 0, .z = 0, .yaw = 0});
  // Only guests send states and get corrections; a roster always has its host.
  CHECK_FALSE(decode(encode(Message{PlayerState{.player = HostPlayerId, .mapId = "m"}})));
  CHECK_FALSE(decode(encode(Message{PlayerState{.player = MaxPlayers, .mapId = "m"}})));
  CHECK_FALSE(decode(encode(
      Message{Roster{.revision = 1, .players = {{.id = 1, .name = "g", .partnerSpecies = {}}}}})));
}

LobbySeat seat(PlayerId id, std::string name = "x", SeatState state = SeatState::Connected) {
  return {.id = id, .name = std::move(name), .state = state};
}

SyncHeld held(PlayerId id, std::string mapId = "m", std::int32_t x = 0) {
  return {.id = id, .mapId = std::move(mapId), .x = x, .y = 0, .z = 0};
}

SyncReport report(std::uint32_t sequence, std::vector<SyncHeld> players = {}) {
  return {.sequence = sequence,
          .snapshotTick = 0,
          .snapshotsLost = 0,
          .snapshotsLate = 0,
          .players = std::move(players)};
}

TEST_CASE("the lobby of up to four seats and the sync diagnostics round-trip and are checked") {
  roundTrip(Lobby{.capacity = 4,
                  .seats = {seat(HostPlayerId, "Mika", SeatState::Ready), seat(1, "Ana"),
                            seat(3, "Leo", SeatState::Ready)},
                  .playing = false,
                  .open = true});
  // A game under way that takes no new players, and the welcome's seat key (#356).
  roundTrip(Lobby{.capacity = 4,
                  .seats = {seat(HostPlayerId, "Mika"), seat(2, "Ana")},
                  .playing = true,
                  .open = false});
  roundTrip(Lobby{.capacity = 3,
                  .seats = {seat(HostPlayerId, "Mika")},
                  .playing = false,
                  .open = true,
                  .difficulty = CoopDifficulty::Hard});
  roundTrip(Lobby{.capacity = 2,
                  .seats = {seat(HostPlayerId, "Mika")},
                  .playing = false,
                  .open = true,
                  .difficulty = CoopDifficulty::Off});
  {
    auto bytes = encode(Message{Lobby{.capacity = 2,
                                      .seats = {seat(HostPlayerId, "Mika")},
                                      .playing = false,
                                      .open = true,
                                      .difficulty = CoopDifficulty::Hard}});
    bytes.pop_back();
    const auto older = decode(bytes);
    REQUIRE(older);
    REQUIRE(older->has_value());
    CHECK(std::get<Lobby>(**older).difficulty == CoopDifficulty::Standard);
    bytes.push_back(3);
    CHECK_FALSE(decode(bytes));
  }
  roundTrip(Welcome{.identity = identity(),
                    .accepted = true,
                    .assignedPlayer = 2,
                    .reason = RejectReason::None,
                    .sessionId = 3,
                    .hostName = {},
                    .seatKey = 0x0123456789ABCDEFULL});
  roundTrip(Welcome{.identity = identity(),
                    .accepted = false,
                    .assignedPlayer = GuestPlayerId,
                    .reason = RejectReason::Closed,
                    .sessionId = 0,
                    .hostName = {},
                    .seatKey = 0});
  roundTrip(SyncWatch{.on = true});
  roundTrip(SyncReport{.sequence = 7,
                       .snapshotTick = 0xFFFFFFF0U,
                       .snapshotsLost = 2,
                       .snapshotsLate = 1,
                       .players = {{.id = 0, .mapId = "mayo01", .x = -10, .y = 0, .z = 30},
                                   {.id = 2, .mapId = "mayo02", .x = 1, .y = 2, .z = 3}}});
  roundTrip(report(0));
  // A lobby always has its host, no more seats than it seats, and no seat twice.
  CHECK_FALSE(decode(
      encode(Message{Lobby{.capacity = 4, .seats = {seat(1)}, .playing = false, .open = true}})));
  CHECK_FALSE(decode(encode(Message{Lobby{
      .capacity = 2, .seats = {seat(0), seat(1), seat(2)}, .playing = false, .open = true}})));
  CHECK_FALSE(decode(
      encode(Message{Lobby{.capacity = 5, .seats = {seat(0)}, .playing = false, .open = true}})));
  CHECK_FALSE(decode(encode(
      Message{Lobby{.capacity = 3, .seats = {seat(0), seat(0)}, .playing = false, .open = true}})));
  CHECK_FALSE(decode(encode(Message{report(1, {held(MaxPlayers)})})));
  CHECK_FALSE(decode(encode(Message{report(1, {held(1, "m", MaxWorldCoordinate + 1)})})));
}

TEST_CASE(
    "each side sends only its own messages: the lobby, the sync watch and report, the world's") {
  net::LoopbackNetwork network{{}};
  auto host = CoopLink::host(network.link(0), identity(), "Host");
  auto guest = CoopLink::guest(network.link(1), identity(), "Guest");
  for (int tick = 0;
       tick < 10 && (host.phase() != LinkPhase::Ready || guest.phase() != LinkPhase::Ready);
       ++tick) {
    network.advance(net::Millis{10});
    host.poll(network.now());
    guest.poll(network.now());
  }
  REQUIRE(host.phase() == LinkPhase::Ready);
  REQUIRE(guest.phase() == LinkPhase::Ready);
  REQUIRE(host.send(Message{Lobby{.capacity = 3,
                                  .seats = {seat(0, "Host")},
                                  .playing = false,
                                  .open = true,
                                  .difficulty = CoopDifficulty::Hard}}));
  REQUIRE(host.send(Message{SyncWatch{.on = true}}));
  REQUIRE(guest.send(Message{report(1)}));
  for (int tick = 0; tick < 5; ++tick) {
    network.advance(net::Millis{10});
    host.poll(network.now());
    guest.poll(network.now());
  }
  const auto lobby = guest.receive();
  REQUIRE(lobby);
  REQUIRE(std::holds_alternative<Lobby>(*lobby));
  CHECK(std::get<Lobby>(*lobby).difficulty == CoopDifficulty::Hard);
  CHECK(guest.receive() == std::optional<Message>{Message{SyncWatch{.on = true}}});
  CHECK(host.receive() == std::optional<Message>{Message{report(1)}});
  // Neither side sends the other's messages.
  CHECK_FALSE(guest.send(
      Message{Lobby{.capacity = 2, .seats = {seat(0)}, .playing = false, .open = true}}));
  CHECK_FALSE(guest.send(Message{SyncWatch{.on = false}}));
  CHECK_FALSE(host.send(Message{report(2)}));
  // The shared world's messages go over the link too (#357): asks and claims to the host, its
  // changes and answers to the guest.
  const Message change{
      WorldChange{.revision = 1, .origin = 0, .ask = 0, .mapId = "m", .edits = {}}};
  const Message claim{
      ItemClaim{.claim = 1, .kind = ClaimKind::Chest, .key = 3, .id = 0, .mapId = "m"}};
  CHECK(guest.send(Message{WorldAsk{.ask = 1, .seen = 0, .mapId = "m", .edits = {}}}));
  CHECK(guest.send(claim));
  CHECK(host.send(change));
  CHECK(host.send(Message{ClaimAnswer{.claim = 1, .granted = true}}));
  CHECK_FALSE(guest.send(change));
  CHECK_FALSE(host.send(claim));
  // The poop piles too: a guest's pile left or eaten to the host, the host's lists and answers
  // back.
  const Message pileAsk{
      PileAsk{.ask = 1, .action = PileAction::Eat, .mapId = "m", .x = 1, .y = 2, .size = 0}};
  const Message piles{PoopPiles{.mapId = "m", .restart = false, .piles = {}}};
  CHECK(guest.send(pileAsk));
  CHECK(host.send(piles));
  CHECK(host.send(Message{PileAnswer{.ask = 1, .granted = false, .size = 0}}));
  CHECK_FALSE(guest.send(piles));
  CHECK_FALSE(host.send(pileAsk));
}

TEST_CASE("cooperative decoder ignores foreign message kinds and rejects damaged frames") {
  CHECK(decode(std::vector<std::uint8_t>{})->has_value() == false);
  CHECK(decode(std::vector<std::uint8_t>{0x10, 0x01})->has_value() == false);
  CHECK_FALSE(decode(std::vector<std::uint8_t>{0xD0, 0x01})->has_value());
  // A kind of the co-op ranges this game does not know is skipped too.
  CHECK_FALSE(decode(std::vector<std::uint8_t>{0x8F, 0x01})->has_value());
  // A known kind cut short is damaged, not foreign.
  CHECK_FALSE(decode(std::vector<std::uint8_t>{PlayerStateKind, 0x01}));

  const auto bytes =
      encode(Message{Commands{.sequence = 5, .field = {.run = true, .moveTarget = std::nullopt}}});
  std::vector<std::uint8_t> truncated = bytes;
  truncated.pop_back();
  CHECK_FALSE(decode(truncated));

  std::vector<std::uint8_t> trailing = bytes;
  trailing.push_back(0xFF);
  CHECK_FALSE(decode(trailing));

  std::vector<std::uint8_t> badFlag = bytes;
  badFlag.at(9) = 2;
  CHECK_FALSE(decode(badFlag));
}

TEST_CASE("cooperative decoder checks identity, snapshots, and view bounds") {
  auto mismatch = encode(Message{
      Hello{.identity = {.protocol = 99, .gameVersion = "1.2.3", .contentHash = "content-hash"},
            .playerName = "Guest",
            .rejoinKey = 0}});
  const auto hello = decode(mismatch);
  REQUIRE(hello);
  CHECK(std::get<Hello>(**hello).identity.protocol == 99);

  Snapshot duplicate{.mapId = "city/field", .entities = {{.networkId = 7}, {.networkId = 7}}};
  CHECK_FALSE(decode(encode(Message{duplicate})));

  Snapshot tooMany{.mapId = "city/field", .entities = {}};
  tooMany.entities.resize(MaxSnapshotEntities + 1);
  CHECK_FALSE(decode(encode(Message{tooMany})));

  const auto badView = encode(Message{ViewState{.player = GuestPlayerId,
                                                .mapId = "city/field",
                                                .left = 100,
                                                .top = 0,
                                                .right = 100,
                                                .bottom = 100}});
  CHECK_FALSE(decode(badView));

  ProfileChunk badChunk{.transferId = 1, .index = 1, .count = 1, .bytes = {0x01}};
  CHECK_FALSE(decode(encode(Message{badChunk})));

  Snapshot atDatagramLimit{.mapId = std::string(255, 'm'), .entities = {}};
  for (std::size_t i = 0; i < MaxSnapshotEntities; ++i) {
    atDatagramLimit.entities.push_back(EntityState{.networkId = static_cast<std::uint16_t>(i)});
  }
  const net::Bytes maximumSnapshot = encode(Message{atDatagramLimit});
  CHECK(maximumSnapshot.size() + 1 <= net::MaxDatagram);
  CHECK(decode(maximumSnapshot));

  JoinState maximumJoin{.mapId = "field", .saveTransferId = 1, .saveChunkCount = 1, .entities = {}};
  for (std::size_t i = 0; i < MaxJoinEntities; ++i) {
    maximumJoin.entities.push_back(EntityState{.networkId = static_cast<std::uint16_t>(i)});
  }
  CHECK(decode(encode(Message{maximumJoin})));
}

TEST_CASE("guest full-save profile chunks assemble out of order and accept exact retries") {
  ProfileAssembler assembler;
  REQUIRE(assembler.begin(44, 3, ProfilePayloadKind::GuestSave));
  const ProfileChunk second{.transferId = 44, .index = 1, .count = 3, .bytes = {3}};
  const auto partial = assembler.push(second);
  REQUIRE(partial);
  CHECK_FALSE(partial->has_value());
  CHECK(assembler.receivedBytes() == 1);

  const auto retry = assembler.push(second);
  REQUIRE(retry);
  CHECK_FALSE(retry->has_value());
  CHECK(assembler.receivedBytes() == 1);

  REQUIRE(assembler.push(ProfileChunk{.transferId = 44, .index = 0, .count = 3, .bytes = {1, 2}}));
  const auto complete =
      assembler.push(ProfileChunk{.transferId = 44, .index = 2, .count = 3, .bytes = {4, 5}});
  REQUIRE(complete);
  REQUIRE(complete->has_value());
  CHECK(**complete == dwe::net::Bytes{1, 2, 3, 4, 5});
  CHECK(assembler.complete());
  CHECK(assembler.receivedBytes() == 5);

  const auto completedRetry = assembler.push(second);
  REQUIRE(completedRetry);
  REQUIRE(completedRetry->has_value());
  CHECK(**completedRetry == dwe::net::Bytes{1, 2, 3, 4, 5});
}

TEST_CASE("guest full-save profile rejects conflicts, invalid lengths, and wrong transfers") {
  ProfileAssembler assembler;
  CHECK_FALSE(assembler.begin(0, 1, ProfilePayloadKind::GuestSave));
  CHECK_FALSE(assembler.begin(1, MaxProfileChunks + 1, ProfilePayloadKind::GuestSave));
  CHECK_FALSE(assembler.begin(1, 1, static_cast<ProfilePayloadKind>(255)));
  REQUIRE(assembler.begin(7, 2, ProfilePayloadKind::GuestSave));

  CHECK_FALSE(assembler.push(ProfileChunk{.transferId = 8, .index = 0, .count = 2, .bytes = {1}}));
  CHECK_FALSE(assembler.push(ProfileChunk{.transferId = 7,
                                          .index = 0,
                                          .count = 2,
                                          .kind = ProfilePayloadKind::HostSave,
                                          .bytes = {1}}));
  REQUIRE(assembler.push(ProfileChunk{.transferId = 7, .index = 0, .count = 2, .bytes = {1}}));
  CHECK_FALSE(assembler.push(ProfileChunk{.transferId = 7, .index = 0, .count = 2, .bytes = {2}}));
  CHECK_FALSE(assembler.push(ProfileChunk{.transferId = 7, .index = 2, .count = 2, .bytes = {3}}));
  CHECK_FALSE(assembler.push(
      ProfileChunk{.transferId = 7,
                   .index = 1,
                   .count = 2,
                   .bytes = std::vector<std::uint8_t>(MaxProfileChunkBytes + 1, 0)}));

  ProfileChunk oversized{.transferId = 9,
                         .index = 0,
                         .count = 1,
                         .bytes = std::vector<std::uint8_t>(MaxProfileChunkBytes + 1, 0)};
  CHECK_FALSE(decode(encode(Message{oversized})));
}

TEST_CASE("cooperative link validates identity and exchanges commands") {
  net::LoopbackNetwork network({});
  CoopLink host = CoopLink::host(network.link(0), identity());
  CoopLink guest = CoopLink::guest(network.link(1), identity(), "Guest");
  for (int i = 0; i < 8; ++i) {
    network.advance(net::Millis{1});
    host.poll(network.now());
    guest.poll(network.now());
  }
  CHECK(host.phase() == LinkPhase::Ready);
  CHECK(guest.phase() == LinkPhase::Ready);
  CHECK(host.remotePlayer() == GuestPlayerId);
  CHECK(guest.remotePlayer() == HostPlayerId);

  const Commands command{.sequence = 1,
                         .field = {.stickX = 120, .run = true, .moveTarget = std::nullopt}};
  REQUIRE(guest.send(Message{command}));
  network.advance(net::Millis{1});
  host.poll(network.now());
  const auto received = host.receive();
  REQUIRE(received);
  if (!received) {
    return;
  }
  CHECK(std::get<Commands>(*received) == command);
  // A reliable message leaves on its sender's poll (net::ChannelPeerLink), not on send.
  REQUIRE(guest.send(Message{Ready{.ready = true}}));
  guest.poll(network.now());
  network.advance(net::Millis{1});
  host.poll(network.now());
  const auto ready = host.receive();
  REQUIRE(ready);
  if (!ready) {
    return;
  }
  CHECK(std::get<Ready>(*ready).ready);
  CHECK_FALSE(host.send(Message{command}));
  CHECK_FALSE(host.send(Message{Ready{.ready = true}}));
  CHECK_FALSE(guest.send(Message{Snapshot{.mapId = "city/field", .entities = {}}}));
  CHECK(host.send(Message{Snapshot{.mapId = "city/field", .entities = {}}}));
  Snapshot oversizedSnapshot{.mapId = std::string(255, 'm'), .entities = {}};
  for (std::size_t i = 0; i <= MaxSnapshotEntities; ++i) {
    oversizedSnapshot.entities.push_back(EntityState{.networkId = static_cast<std::uint16_t>(i)});
  }
  CHECK_FALSE(host.send(Message{oversizedSnapshot}));
  guest.leave();
  network.advance(net::Millis{1});
  host.poll(network.now());
  CHECK(host.phase() == LinkPhase::Left);
}

TEST_CASE("cooperative link rejects incompatible game content") {
  net::LoopbackNetwork network({});
  Identity guestIdentity = identity();
  guestIdentity.contentHash = "different-content";
  CoopLink host = CoopLink::host(network.link(0), identity());
  CoopLink guest = CoopLink::guest(network.link(1), guestIdentity, "Guest");
  for (int i = 0; i < 8; ++i) {
    network.advance(net::Millis{1});
    host.poll(network.now());
    guest.poll(network.now());
  }
  CHECK(host.phase() == LinkPhase::Rejected);
  CHECK(host.failure() == LinkFailure::ContentMismatch);
  CHECK(guest.phase() == LinkPhase::Rejected);
  CHECK(guest.failure() == LinkFailure::ContentMismatch);
}

TEST_CASE("cooperative handshake and silence timeouts end the link") {
  {
    net::LoopbackNetwork network({});
    CoopLink guest = CoopLink::guest(network.link(1), identity(), "Guest");
    guest.poll(network.now());
    network.advance(net::Millis{10000});
    guest.poll(network.now());
    CHECK(guest.phase() == LinkPhase::Failed);
    CHECK(guest.failure() == LinkFailure::NoAnswer);
  }
  {
    net::LoopbackNetwork network({});
    CoopLink host = CoopLink::host(network.link(0), identity());
    CoopLink guest = CoopLink::guest(network.link(1), identity(), "Guest");
    for (int i = 0; i < 8; ++i) {
      network.advance(net::Millis{1});
      host.poll(network.now());
      guest.poll(network.now());
    }
    REQUIRE(guest.phase() == LinkPhase::Ready);
    // The host goes without a word: nothing more comes.
    network.cut();
    network.advance(net::Millis{8000});
    guest.poll(network.now());
    CHECK(guest.phase() == LinkPhase::Failed);
    CHECK(guest.failure() == LinkFailure::Silence);
  }
}

namespace {

// A transport whose incoming messages and state the test sets.
class ScriptedPeer final : public net::PeerLink {
public:
  void send(net::Lane /*lane*/, std::span<const std::uint8_t> /*message*/) override {}
  std::optional<net::Bytes> receive(net::Lane lane) override {
    auto& queue = lane == net::Lane::Reliable ? reliable : unreliable;
    if (queue.empty()) {
      return std::nullopt;
    }
    net::Bytes bytes = std::move(queue.front());
    queue.erase(queue.begin());
    return bytes;
  }
  void poll(net::Millis /*now*/) override {}
  [[nodiscard]] net::PeerState state() const override { return peerState; }
  [[nodiscard]] net::PeerFailure failure() const override {
    return peerState == net::PeerState::Failed ? net::PeerFailure::Closed : net::PeerFailure::None;
  }
  [[nodiscard]] std::optional<net::Millis> lastHeard() const override { return net::Millis{0}; }

  std::vector<net::Bytes> reliable;
  std::vector<net::Bytes> unreliable;
  net::PeerState peerState = net::PeerState::Open;
};

net::Bytes welcome() {
  return encode(Message{Welcome{.identity = identity(),
                                .accepted = true,
                                .assignedPlayer = GuestPlayerId,
                                .reason = RejectReason::None,
                                .sessionId = 1,
                                .hostName = "Host",
                                .seatKey = 0}});
}

} // namespace

TEST_CASE("a link keeps how it ended whatever its transport does next") {
  SUBCASE("the host's goodbye and the closing transport in one poll read as the host leaving") {
    ScriptedPeer peer;
    CoopLink guest = CoopLink::guest(peer, identity(), "Guest");
    peer.reliable.push_back(welcome());
    guest.poll(net::Millis{1});
    REQUIRE(guest.phase() == LinkPhase::Ready);
    peer.reliable.push_back(encode(Message{Leave{LeaveReason::Quit}}));
    peer.peerState = net::PeerState::Failed;
    guest.poll(net::Millis{2});
    CHECK(guest.phase() == LinkPhase::Left);
    CHECK(guest.leaveReason() == LeaveReason::Quit);
    guest.poll(net::Millis{3});
    CHECK(guest.phase() == LinkPhase::Left);
    CHECK(guest.failure() == LinkFailure::None);
  }
  SUBCASE("a damaged message ends the link; a goodbye after it changes nothing") {
    ScriptedPeer peer;
    CoopLink guest = CoopLink::guest(peer, identity(), "Guest");
    peer.reliable.push_back(welcome());
    guest.poll(net::Millis{1});
    REQUIRE(guest.phase() == LinkPhase::Ready);
    peer.reliable.push_back(net::Bytes{WelcomeKind});
    peer.reliable.push_back(encode(Message{Leave{LeaveReason::Quit}}));
    guest.poll(net::Millis{2});
    CHECK(guest.phase() == LinkPhase::Failed);
    CHECK(guest.failure() == LinkFailure::Damaged);
    CHECK_FALSE(guest.leaveReason());
  }
  SUBCASE("a refused guest keeps the host's reason") {
    ScriptedPeer peer;
    CoopLink guest = CoopLink::guest(peer, identity(), "Guest");
    peer.reliable.push_back(encode(Message{Welcome{.identity = identity(),
                                                   .accepted = false,
                                                   .assignedPlayer = GuestPlayerId,
                                                   .reason = RejectReason::RoomFull,
                                                   .sessionId = 0,
                                                   .hostName = {},
                                                   .seatKey = 0}}));
    peer.reliable.push_back(encode(Message{Leave{LeaveReason::SessionEnded}}));
    peer.peerState = net::PeerState::Failed;
    guest.poll(net::Millis{1});
    CHECK(guest.phase() == LinkPhase::Rejected);
    CHECK(guest.failure() == LinkFailure::RoomFull);
  }
}

TEST_CASE("a side busy with a long load keeps its link, and one that hangs is dropped in time") {
  // `stalled`: which side stops polling (0 the host, 1 the guest); `stall`: for how long.
  const auto run = [](std::size_t stalled, net::Millis stall) {
    net::LoopbackNetwork network({.latency = net::Millis{20},
                                  .jitter = net::Millis{10},
                                  .lossPercent = 5,
                                  .duplicatePercent = 0});
    CoopLink host = CoopLink::host(network.link(0), identity());
    CoopLink guest = CoopLink::guest(network.link(1), identity(), "Guest");
    std::array<CoopLink*, 2> sides{&host, &guest};
    const auto step = [&](bool both) {
      network.advance(net::Millis{50});
      for (std::size_t side = 0; side < sides.size(); ++side) {
        if (both || side != stalled) {
          sides.at(side)->poll(network.now());
        }
      }
    };
    for (int i = 0; i < 40; ++i) {
      step(true);
    }
    REQUIRE(host.phase() == LinkPhase::Ready);
    REQUIRE(guest.phase() == LinkPhase::Ready);
    // The stalled side's game is busy (it neither polls nor sends); the other goes on.
    const net::Millis until = network.now() + stall;
    while (network.now() < until && sides.at(1 - stalled)->phase() == LinkPhase::Ready) {
      step(false);
    }
    const LinkPhase waited = sides.at(1 - stalled)->phase();
    const LinkFailure why = sides.at(1 - stalled)->failure();
    for (int i = 0; i < 40; ++i) {
      step(true);
    }
    return std::tuple{waited, why, sides.at(stalled)->phase()};
  };
  for (const std::size_t stalled : {std::size_t{0}, std::size_t{1}}) {
    CAPTURE(stalled);
    // A 30 s load: both sides still hold the link once it ends.
    const auto [waited, why, after] = run(stalled, net::Millis{30000});
    CHECK(waited == LinkPhase::Ready);
    CHECK(after == LinkPhase::Ready);
    // A game that hangs: past LinkTimeouts::busy and the silence, the other side lets it go.
    const auto hung = run(stalled, net::Millis{120000});
    CHECK(std::get<0>(hung) == LinkPhase::Failed);
    CHECK(std::get<1>(hung) == LinkFailure::Silence);
  }
}

TEST_CASE("remote command buffer rejects old sequences and expires to neutral") {
  RemoteCommandBuffer buffer;
  CHECK(buffer.push(
      Commands{.sequence = 0xFFFFFFFFu, .field = {.stickY = 50, .moveTarget = std::nullopt}},
      net::Millis{100}));
  CHECK_FALSE(buffer.push(Commands{.sequence = 0xFFFFFFFEu, .field = {}}, net::Millis{110}));
  CHECK(buffer.push(Commands{.sequence = 0u, .field = {.stickX = -100, .moveTarget = std::nullopt}},
                    net::Millis{120}));
  CHECK(buffer.current(net::Millis{300}).stickX == -100);
  CHECK(buffer.current(net::Millis{371}) == FieldCommand{});
}

TEST_CASE("an accepted welcome tells the guest the host's name; a rejected one reads as before") {
  roundTrip(Welcome{.identity = identity(),
                    .accepted = true,
                    .assignedPlayer = GuestPlayerId,
                    .reason = RejectReason::None,
                    .sessionId = 7,
                    .hostName = "José",
                    .seatKey = 0});
  // A rejected welcome has no name: the same bytes as the first protocol's, which an older guest
  // reads to say why it was turned away.
  const Welcome rejected{.identity = identity(),
                         .accepted = false,
                         .assignedPlayer = GuestPlayerId,
                         .reason = RejectReason::VersionMismatch,
                         .sessionId = 0,
                         .hostName = "Mika",
                         .seatKey = 0};
  const auto bytes = encode(Message{rejected});
  const auto plain = encode(Message{Welcome{.identity = identity(),
                                            .accepted = false,
                                            .assignedPlayer = GuestPlayerId,
                                            .reason = RejectReason::VersionMismatch,
                                            .sessionId = 0,
                                            .hostName = {},
                                            .seatKey = 0}});
  CHECK(bytes == plain);
  // An older host's accepted welcome (no name) still reads, with no name.
  const auto older = decode(encode(Message{Welcome{.identity = identity(),
                                                   .accepted = true,
                                                   .assignedPlayer = GuestPlayerId,
                                                   .reason = RejectReason::None,
                                                   .sessionId = 9,
                                                   .hostName = {},
                                                   .seatKey = 0}}));
  REQUIRE(older);
  REQUIRE(older->has_value());
  CHECK(std::get<Welcome>(**older).hostName.empty());
}

TEST_CASE("each side of a cooperative link knows the other's name") {
  net::LoopbackNetwork network({});
  CoopLink host = CoopLink::host(network.link(0), identity(), "Mika");
  CoopLink guest = CoopLink::guest(network.link(1), identity(), "Ana");
  for (int i = 0; i < 8; ++i) {
    network.advance(net::Millis{1});
    host.poll(network.now());
    guest.poll(network.now());
  }
  REQUIRE(guest.phase() == LinkPhase::Ready);
  CHECK(host.remoteName() == "Ana");
  CHECK(guest.remoteName() == "Mika");
}

TEST_CASE("the host's admission seats a returning guest by its key and refuses a closed game") {
  const auto connect = [](CoopLink& host, CoopLink& guest, net::LoopbackNetwork& network) {
    for (int i = 0; i < 8 && guest.phase() == LinkPhase::Connecting; ++i) {
      network.advance(net::Millis{1});
      host.poll(network.now());
      guest.poll(network.now());
    }
  };
  std::vector<Hello> heard;
  const auto decide = [&](const Hello& hello) {
    heard.push_back(hello);
    if (hello.rejoinKey == 77) {
      return Admission{.refused = RejectReason::None, .id = 3, .seatKey = 77};
    }
    return Admission{.refused = RejectReason::Closed, .id = GuestPlayerId, .seatKey = 0};
  };
  // Back with its key: the welcome gives it its own seat and the same key again.
  net::LoopbackNetwork back({});
  CoopLink host = CoopLink::host(back.link(0), identity(), "Mika");
  host.admit(decide);
  CoopLink returning =
      CoopLink::guest(back.link(1), identity(), "Ana", LinkTimeouts{}, std::uint64_t{77});
  connect(host, returning, back);
  REQUIRE(returning.phase() == LinkPhase::Ready);
  CHECK(returning.localPlayer() == 3);
  CHECK(returning.seatKey() == 77);
  CHECK(host.guestId() == 3);
  CHECK(host.seatKey() == 77);
  REQUIRE(heard.size() == 1);
  CHECK(heard.front().rejoinKey == 77);
  // A new player while the game is closed to them is turned away with why.
  net::LoopbackNetwork closed({});
  CoopLink closedHost = CoopLink::host(closed.link(0), identity(), "Mika");
  closedHost.admit(decide);
  CoopLink stranger = CoopLink::guest(closed.link(1), identity(), "Leo");
  connect(closedHost, stranger, closed);
  CHECK(stranger.phase() == LinkPhase::Rejected);
  CHECK(stranger.failure() == LinkFailure::RoomClosed);
  CHECK(closedHost.phase() == LinkPhase::Rejected);
}
