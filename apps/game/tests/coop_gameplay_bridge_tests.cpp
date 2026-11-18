#include "coop_gameplay_bridge.hpp"

#include <dwe/net/loopback.hpp>

#include <doctest/doctest.h>

using namespace dwe::app;

namespace {
dwe::game::coop::Identity testIdentity() {
  return {.protocol = dwe::game::coop::ProtocolVersion,
          .gameVersion = "test",
          .contentHash = "content"};
}
} // namespace

TEST_CASE("coop gameplay bridge routes guest movement into the host controller") {
  using namespace dwe::game::coop;
  dwe::net::LoopbackNetwork network({});
  CoopLink hostLink = CoopLink::host(network.link(0), testIdentity());
  CoopLink guestLink = CoopLink::guest(network.link(1), testIdentity(), "Guest");
  NetworkFieldController remoteController;
  CoopGameplayBridge host{hostLink, remoteController};
  NetworkFieldController unusedController;
  CoopGameplayBridge guest{guestLink, unusedController};

  for (int i = 0; i < 8; ++i) {
    network.advance(dwe::net::Millis{1});
    host.poll(network.now());
    guest.poll(network.now());
  }
  REQUIRE(hostLink.phase() == LinkPhase::Ready);
  REQUIRE(guestLink.phase() == LinkPhase::Ready);

  PlayerCommands local;
  local.sequence = 23;
  local.field.stick = {0.25F, -1.0F};
  local.field.run = true;
  local.field.moveTarget = glm::vec3{120.0F, 0.0F, -45.0F};
  REQUIRE(guest.sendLocalField(local));

  network.advance(dwe::net::Millis{1});
  host.poll(network.now());
  const PlayerCommands received = remoteController.next(0);
  // The bridge owns the outgoing sequence: this is the fix that lets the host keep accepting
  // continuous guest commands (its RemoteCommandBuffer rejects equal or older sequences). The
  // caller's `sequence` is ignored and the bridge stamps its own monotonic value, starting at 1.
  CHECK(received.sequence == 1);
  CHECK(received.field.stick.x == doctest::Approx(0.25F).epsilon(0.0001));
  CHECK(received.field.stick.y == -1.0F);
  CHECK(received.field.run);
  REQUIRE(received.field.moveTarget);
  CHECK(received.field.moveTarget->x == 120.0F);
  CHECK(received.field.moveTarget->z == -45.0F);

  // A second command the host has not seen increments the bridge sequence, so the host accepts it
  // instead of treating it as a stale repeat.
  REQUIRE(guest.sendLocalField(local));
  network.advance(dwe::net::Millis{1});
  host.poll(network.now());
  CHECK(remoteController.next(0).sequence == 2);

  const Snapshot snapshot{.mapId = "field/main",
                          .worldTick = 90,
                          .acknowledgedCommand = 23,
                          .entities = {{.networkId = 1, .owner = HostPlayerId,
                                        .role = EntityRole::Player, .x = 10},
                                       {.networkId = 3, .owner = GuestPlayerId,
                                        .role = EntityRole::Player, .x = 120}}};
  CHECK_FALSE(guest.sendHostSnapshot(snapshot));
  REQUIRE(host.sendHostSnapshot(snapshot));
  network.advance(dwe::net::Millis{1});
  guest.poll(network.now());
  const auto receivedSnapshot = guest.presentedWorld(1.0F);
  REQUIRE(receivedSnapshot);
  CHECK(receivedSnapshot->mapId == snapshot.mapId);
  REQUIRE(receivedSnapshot->entities.size() == 2);
  CHECK(receivedSnapshot->entities[1].networkId == 3);
  CHECK(receivedSnapshot->entities[1].position.x == doctest::Approx(120.0F));
}

TEST_CASE("coop gameplay bridge transfers a full guest save in reliable bounded chunks") {
  using namespace dwe::game::coop;
  dwe::net::LoopbackNetwork network({});
  CoopLink hostLink = CoopLink::host(network.link(0), testIdentity());
  CoopLink guestLink = CoopLink::guest(network.link(1), testIdentity(), "Guest");
  NetworkFieldController hostController;
  NetworkFieldController guestController;
  CoopGameplayBridge host{hostLink, hostController};
  CoopGameplayBridge guest{guestLink, guestController};
  for (int i = 0; i < 8; ++i) {
    network.advance(dwe::net::Millis{1});
    host.poll(network.now());
    guest.poll(network.now());
  }
  REQUIRE(hostLink.phase() == LinkPhase::Ready);

  dwe::net::Bytes save(2501);
  for (std::size_t i = 0; i < save.size(); ++i) save[i] = static_cast<std::uint8_t>(i % 251);
  REQUIRE(guest.sendProfile(ProfilePayloadKind::GuestSave, 42, save));
  for (int i = 0; i < 20; ++i) {
    network.advance(dwe::net::Millis{1});
    host.poll(network.now());
    guest.poll(network.now());
  }
  auto profile = host.receiveProfile();
  REQUIRE(profile);
  CHECK(profile->kind == ProfilePayloadKind::GuestSave);
  CHECK(profile->transferId == 42);
  CHECK(profile->bytes == save);
}
