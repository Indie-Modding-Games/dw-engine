#include <dwe/game/coop/link.hpp>
#include <dwe/game/coop/profile_transfer.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/loopback.hpp>
#include <dwe/net/wire.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
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
  roundTrip(Hello{.identity = identity(), .playerName = "Guest"});
  roundTrip(Welcome{.identity = identity(),
                    .accepted = true,
                    .assignedPlayer = GuestPlayerId,
                    .reason = RejectReason::None,
                    .sessionId = 0x1020304050607080ULL,
                    .hostName = {}});
  roundTrip(Welcome{.identity = identity(),
                    .accepted = false,
                    .assignedPlayer = GuestPlayerId,
                    .reason = RejectReason::RoomFull,
                    .sessionId = 42,
                    .hostName = {}});
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

TEST_CASE("cooperative decoder ignores foreign message kinds and rejects damaged frames") {
  CHECK(decode(std::vector<std::uint8_t>{})->has_value() == false);
  CHECK(decode(std::vector<std::uint8_t>{0x10, 0x01})->has_value() == false);
  CHECK_FALSE(decode(std::vector<std::uint8_t>{0xC0, 0x01})->has_value());
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
            .playerName = "Guest"}});
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
    network.advance(net::Millis{8000});
    guest.poll(network.now());
    CHECK(guest.phase() == LinkPhase::Failed);
    CHECK(guest.failure() == LinkFailure::Silence);
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
                    .hostName = "José"});
  // A rejected welcome has no name: the same bytes as the first protocol's, which an older guest
  // reads to say why it was turned away.
  const Welcome rejected{.identity = identity(),
                         .accepted = false,
                         .assignedPlayer = GuestPlayerId,
                         .reason = RejectReason::VersionMismatch,
                         .sessionId = 0,
                         .hostName = "Mika"};
  const auto bytes = encode(Message{rejected});
  const auto plain = encode(Message{Welcome{.identity = identity(),
                                            .accepted = false,
                                            .assignedPlayer = GuestPlayerId,
                                            .reason = RejectReason::VersionMismatch,
                                            .sessionId = 0,
                                            .hostName = {}}});
  CHECK(bytes == plain);
  // An older host's accepted welcome (no name) still reads, with no name.
  const auto older = decode(encode(Message{Welcome{.identity = identity(),
                                                   .accepted = true,
                                                   .assignedPlayer = GuestPlayerId,
                                                   .reason = RejectReason::None,
                                                   .sessionId = 9,
                                                   .hostName = {}}}));
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
