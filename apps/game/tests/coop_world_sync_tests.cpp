#include "coop_gameplay_bridge.hpp"
#include "coop_world_sync.hpp"
#include "player_commands.hpp"
#include "player_registry.hpp"

#include <dwe/net/loopback.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace {

using namespace dwe::app;
namespace coop = dwe::game::coop;

coop::Identity testIdentity() {
  return {.protocol = coop::ProtocolVersion, .gameVersion = "test", .contentHash = "content"};
}

coop::Snapshot threeBodies(std::string_view mapId, std::uint32_t tick, std::uint32_t ack) {
  return coop::Snapshot{
      .mapId = std::string{mapId},
      .worldTick = tick,
      .acknowledgedCommand = ack,
      .entities = {
          coop::EntityState{.networkId = 1,
                            .owner = coop::HostPlayerId,
                            .role = coop::EntityRole::Player,
                            .x = 1000},
          coop::EntityState{.networkId = 2,
                            .owner = coop::HostPlayerId,
                            .role = coop::EntityRole::Partner,
                            .x = 900},
          coop::EntityState{.networkId = 3,
                            .owner = coop::GuestPlayerId,
                            .role = coop::EntityRole::Player,
                            .x = 2500},
      }};
}

PresentedCoopEntity entity(std::uint16_t id,
                           coop::PlayerId owner,
                           coop::EntityRole role,
                           glm::vec3 position,
                           coop::Motion motion = coop::Motion::Idle) {
  return PresentedCoopEntity{.networkId = id,
                             .owner = owner,
                             .role = role,
                             .position = position,
                             .yaw = 0.5F,
                             .motion = motion};
}

} // namespace

TEST_CASE("presentedRoles yields the host bodies when the field and the sample agree on the map") {
  const PresentedCoopWorld world{
      .mapId = "twna01",
      .worldTick = 10,
      .acknowledgedCommand = 4,
      .entities = {
          entity(1, coop::HostPlayerId, coop::EntityRole::Player, {1000.0F, 0.0F, 0.0F},
                 coop::Motion::Walk),
          entity(2, coop::HostPlayerId, coop::EntityRole::Partner, {900.0F, 0.0F, 0.0F},
                 coop::Motion::Walk),
          entity(3, coop::GuestPlayerId, coop::EntityRole::Player, {2500.0F, 0.0F, 0.0F},
                 coop::Motion::Run),
      }};
  const CoopPresentedRoles roles = presentedRoles(world, "twna01");
  REQUIRE(roles.hostPlayer);
  REQUIRE(roles.hostPartner);
  REQUIRE(roles.guestAuthoritative);
  CHECK(roles.hostPlayer->position == glm::vec3{1000.0F, 0.0F, 0.0F});
  CHECK(roles.hostPartner->position == glm::vec3{900.0F, 0.0F, 0.0F});
  CHECK(roles.guestAuthoritative->position == glm::vec3{2500.0F, 0.0F, 0.0F});
  CHECK(roles.hostPlayer->motion == dwe::scene::Motion::Walk);
  CHECK(roles.guestAuthoritative->motion == dwe::scene::Motion::Run);
}

TEST_CASE("presentedRoles locks the shadow-without-character state when the maps differ") {
  // Reproduce headlessly what the user sees: host is on "twna02" (the snapshot says so) while the
  // client's field is still on "twna01". The helper must refuse to describe any host body so the
  // draw path withholds the model (field_scene.cpp gates `submitCoopPlayer` on the presentation
  // being set) while `drawShadows` continues from the empty host-slot transform: shadow, no mesh.
  const PresentedCoopWorld world{
      .mapId = "twna02",
      .worldTick = 11,
      .acknowledgedCommand = 5,
      .entities = {
          entity(1, coop::HostPlayerId, coop::EntityRole::Player, {100.0F, 0.0F, 100.0F}),
          entity(2, coop::HostPlayerId, coop::EntityRole::Partner, {90.0F, 0.0F, 90.0F}),
          entity(3, coop::GuestPlayerId, coop::EntityRole::Player, {110.0F, 0.0F, 110.0F}),
      }};
  const CoopPresentedRoles roles = presentedRoles(world, "twna01");
  CHECK_FALSE(roles.hostPlayer);
  CHECK_FALSE(roles.hostPartner);
  CHECK_FALSE(roles.guestAuthoritative);
}

TEST_CASE("presentedRoles hands the reconcile the guest authoritative state when the map matches") {
  // The reconcile logic (added later) needs the host's authoritative guest position from a same-map
  // snapshot to compare against the client's predicted guest. Locking this shape now so a
  // regression in the snapshot's guest entity surfaces here before it reaches the field.
  const PresentedCoopWorld world{
      .mapId = "twna01",
      .worldTick = 12,
      .acknowledgedCommand = 6,
      .entities = {
          entity(3, coop::GuestPlayerId, coop::EntityRole::Player, {2500.0F, 0.0F, 0.0F}),
      }};
  const CoopPresentedRoles roles = presentedRoles(world, "twna01");
  REQUIRE(roles.guestAuthoritative);
  CHECK(roles.guestAuthoritative->position.x == doctest::Approx(2500.0F));
  CHECK_FALSE(roles.hostPlayer);
}

TEST_CASE("coop loopback hands the client a snapshot whose roles can be extracted headlessly") {
  // End-to-end but no window, no second machine, no autotest route: real `CoopLink`s, real
  // `CoopGameplayBridge` handshake, real snapshot crossing `net::LoopbackNetwork`, real
  // `presentedRoles` decision on the other side. This is the fast repro for the shadow bug.
  dwe::net::LoopbackNetwork network({});
  coop::CoopLink hostLink = coop::CoopLink::host(network.link(0), testIdentity());
  coop::CoopLink guestLink = coop::CoopLink::guest(network.link(1), testIdentity(), "Guest");
  NetworkFieldController hostController;
  NetworkFieldController guestController;
  CoopGameplayBridge host{hostLink, hostController};
  CoopGameplayBridge guest{guestLink, guestController};
  for (int i = 0; i < 8; ++i) {
    network.advance(dwe::net::Millis{1});
    host.poll(network.now());
    guest.poll(network.now());
  }
  REQUIRE(hostLink.phase() == coop::LinkPhase::Ready);
  REQUIRE(guestLink.phase() == coop::LinkPhase::Ready);

  PlayerCommands movement;
  movement.field.stick = {0.0F, -1.0F};
  REQUIRE(guest.sendLocalField(movement));
  REQUIRE(host.sendHostSnapshot(threeBodies("twna01", /*tick=*/100, /*ack=*/1)));

  for (int i = 0; i < 4; ++i) {
    network.advance(dwe::net::Millis{10});
    host.poll(network.now());
    guest.poll(network.now());
  }

  const auto world = guest.presentedWorld(1.0F);
  REQUIRE(world);
  CHECK(world->mapId == "twna01");

  // Same map: everything the client needs to present and, in a later pass, to reconcile.
  const CoopPresentedRoles onMap = presentedRoles(*world, "twna01");
  CHECK(onMap.hostPlayer.has_value());
  CHECK(onMap.hostPartner.has_value());
  CHECK(onMap.guestAuthoritative.has_value());
  // The host's authoritative guest x is exactly the quantized value the host put in the snapshot.
  CHECK(onMap.guestAuthoritative->position.x == doctest::Approx(2500.0F));

  // Different map: the shadow-without-character state, reproduced in-process.
  const CoopPresentedRoles offMap = presentedRoles(*world, "twna02");
  CHECK_FALSE(offMap.hostPlayer.has_value());
  CHECK_FALSE(offMap.hostPartner.has_value());
  CHECK_FALSE(offMap.guestAuthoritative.has_value());
}

TEST_CASE("two scripted players drive independent commands through one registry") {
  // The harness the reconcile logic and future interaction tests build on: two `FieldPlayer`s in
  // one process with a `ScriptedController` for the guest. Any regression in the second player's
  // commands, presence or independent state surfaces here deterministically, no network.
  PlayerRegistry players{"Host"};
  PlayerCommands guestForward;
  guestForward.field.stick = {0.0F, -1.0F};
  std::vector<PlayerCommands> guestScript(60, guestForward);
  REQUIRE(players.addGuest("Guest", std::make_unique<ScriptedController>(std::move(guestScript)),
                           PlayerLink::Connected));

  const auto hostCommand = players.next(HostPlayerId, 0);
  const auto guestCommand = players.next(GuestPlayerId, 0);
  REQUIRE(hostCommand.has_value());
  REQUIRE(guestCommand.has_value());
  CHECK(hostCommand->field.stick == glm::vec2{0.0F, 0.0F});
  CHECK(guestCommand->field.stick == glm::vec2{0.0F, -1.0F});
}
