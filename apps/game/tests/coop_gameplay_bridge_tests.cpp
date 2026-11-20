#include <dwe/game/field/field.hpp>
#include <dwe/net/loopback.hpp>
#include <dwe/scene/world.hpp>

#include <doctest/doctest.h>

#include <algorithm>

#include "coop_gameplay_bridge.hpp"
#include "coop_snapshot.hpp"
#include "packages/game/field/tests/fixture.hpp"

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
  CHECK(received.sequence == 23);
  CHECK(received.field.stick.x == doctest::Approx(0.25F).epsilon(0.0001));
  CHECK(received.field.stick.y == -1.0F);
  CHECK(received.field.run);
  REQUIRE(received.field.moveTarget);
  CHECK(received.field.moveTarget->x == 120.0F);
  CHECK(received.field.moveTarget->z == -45.0F);

  const Snapshot snapshot{
      .mapId = "field/main",
      .worldTick = 90,
      .acknowledgedCommand = 23,
      .entities = {{.networkId = 1, .owner = HostPlayerId, .role = EntityRole::Player, .x = 10},
                   {.networkId = 3, .owner = GuestPlayerId, .role = EntityRole::Player, .x = 120}}};
  CHECK_FALSE(guest.sendHostSnapshot(snapshot));
  REQUIRE(host.sendHostSnapshot(snapshot));
  network.advance(dwe::net::Millis{1});
  guest.poll(network.now());
  const auto receivedSnapshot = guest.presentedWorld(network.now());
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
  for (std::size_t i = 0; i < save.size(); ++i)
    save[i] = static_cast<std::uint8_t>(i % 251);
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

TEST_CASE("each game sees the other player's partner following them, interpolated") {
  // The host's field moves the guest and the guest's partner; its snapshots carry both pairs, so
  // the guest draws the host's partner and the host draws the guest's partner from its own field.
  using namespace dwe::game::coop;
  dwe::net::LoopbackNetwork network({});
  CoopLink hostLink = CoopLink::host(network.link(0), testIdentity());
  CoopLink guestLink = CoopLink::guest(network.link(1), testIdentity(), "Guest");
  NetworkFieldController guestOnHost;
  CoopGameplayBridge host{hostLink, guestOnHost};
  NetworkFieldController unused;
  CoopGameplayBridge guest{guestLink, unused};
  for (int i = 0; i < 8; ++i) {
    network.advance(dwe::net::Millis{1});
    host.poll(network.now());
    guest.poll(network.now());
  }
  REQUIRE(guestLink.phase() == LinkPhase::Ready);

  dwe::scene::World world;
  dwe::game::field::Field field{world, dwe::game::field::testing::originalRules()};
  field.enterMapAt(dwe::game::field::testing::roomMap("room"),
                   {.position = {-1500.0F, 0.0F, 0.0F}, .yaw = 0.0F});
  REQUIRE(field.addPlayer(dwe::game::coop::GuestPlayerId,
                          {.position = {0.0F, 0.0F, 0.0F}, .yaw = 0.0F}));
  REQUIRE(field.addPartner(dwe::game::coop::GuestPlayerId, "models/guest_partner", 120.0F));
  const glm::vec3 startedAt = field.partner(dwe::game::coop::GuestPlayerId)->position;
  REQUIRE(field.setInput(dwe::game::coop::GuestPlayerId, {.stick = {1.0F, 0.0F}, .run = true}));

  const auto partnerOf = [](const PresentedCoopWorld& world, dwe::game::coop::PlayerId owner) {
    const auto found = std::ranges::find_if(world.entities, [&](const auto& entity) {
      return entity.owner == owner && entity.role == EntityRole::Partner;
    });
    return found == world.entities.end() ? std::optional<PresentedCoopEntity>{}
                                         : std::optional<PresentedCoopEntity>{*found};
  };
  std::uint32_t worldTick = 0;
  const auto send = [&] {
    auto snapshot = makeCoopSnapshot(field, field.mapId(), ++worldTick, 0);
    REQUIRE(snapshot);
    REQUIRE(host.sendHostSnapshot(*snapshot));
    network.advance(dwe::net::Millis{1});
    guest.poll(network.now());
  };
  for (int tick = 0; tick < 60; ++tick) {
    field.tick(1.0F / 60.0F);
  }
  send();
  for (int tick = 0; tick < 3; ++tick) {
    field.tick(1.0F / 60.0F);
  }
  send();

  // The host stamped the snapshots 50 ms apart (world ticks 1 and 2): drawn 100 ms behind the
  // quickest delivery, `received` + 50 shows the first, + 100 the second, + 75 halfway.
  const dwe::net::Millis received = network.now();
  const auto before = guest.presentedWorld(received + dwe::net::Millis{50});
  const auto middle = guest.presentedWorld(received + dwe::net::Millis{75});
  const auto after = guest.presentedWorld(received + dwe::net::Millis{100});
  REQUIRE(before);
  REQUIRE(middle);
  REQUIRE(after);
  CHECK(after->entities.size() == 4);
  const auto hostPartner = partnerOf(*after, dwe::game::coop::HostPlayerId);
  REQUIRE(hostPartner);
  CHECK(hostPartner->networkId == 2);
  const auto guestBefore = partnerOf(*before, dwe::game::coop::GuestPlayerId);
  const auto guestMiddle = partnerOf(*middle, dwe::game::coop::GuestPlayerId);
  const auto guestAfter = partnerOf(*after, dwe::game::coop::GuestPlayerId);
  REQUIRE(guestBefore);
  REQUIRE(guestMiddle);
  REQUIRE(guestAfter);
  CHECK(guestAfter->networkId == 4);
  // It ran after the guest, and between two snapshots it is drawn between them.
  CHECK(guestAfter->position.x > startedAt.x + 300.0F);
  CHECK(guestAfter->motion != dwe::game::coop::Motion::Idle);
  CHECK(guestAfter->position.x > guestBefore->position.x);
  CHECK(guestMiddle->position.x > guestBefore->position.x);
  CHECK(guestMiddle->position.x < guestAfter->position.x);
}
