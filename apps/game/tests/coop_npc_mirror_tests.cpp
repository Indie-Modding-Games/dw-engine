// The host's map NPCs on a guest's screen: where they stand and when they leave.

#include <dwe/game/field/field.hpp>
#include <dwe/scene/world.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <numbers>

#include "coop_npc_mirror.hpp"
#include "coop_snapshot.hpp"
#include "packages/game/field/tests/fixture.hpp"

using namespace dwe::app;
namespace coop = dwe::game::coop;
using dwe::game::field::Field;
using dwe::game::field::testing::originalRules;
using dwe::game::field::testing::roomMap;
using dwe::net::Millis;

namespace {

constexpr float Tick = 1.0F / 60.0F;

dwe::assets::MapDocument wanderers() {
  dwe::assets::MapDocument map = roomMap("room");
  for (const float x : {800.0F, -800.0F}) {
    dwe::assets::MapNpc npc;
    npc.typeId = 1;
    npc.position = {static_cast<std::int32_t>(x), 0, 0};
    npc.scriptId = static_cast<std::int32_t>(map.npcs.size()) + 5;
    npc.hp = 10;
    npc.currentHp = 10;
    npc.aiSections = {1, 0, 1, 1, -1, -1, -1, -1};
    npc.waypoints = {{static_cast<std::int32_t>(x), 0, -1500}, {0, 0, 0}};
    map.npcs.push_back(npc);
  }
  return map;
}

struct Side {
  dwe::scene::World world;
  Field field{world, originalRules()};

  Side() {
    field.enterMapAt(wanderers(), {.position = {0.0F, 0.0F, 1500.0F}, .yaw = 0.0F});
    for (std::size_t slot = 0; slot < 2; ++slot) {
      REQUIRE(field.npcs().spawn(
          {.slot = slot, .species = 1, .autotalk = false, .model = "npc", .radius = 100.0F}));
    }
  }

  void run(float seconds) {
    for (float left = seconds; left > 0.0F; left -= Tick) {
      field.tick(Tick);
    }
  }
};

PresentedCoopWorld drawn(const Field& host) {
  const auto snapshot = makeCoopSnapshot(host, "room", 1, 0);
  REQUIRE(snapshot);
  PresentedCoopWorld world{.mapId = snapshot->mapId,
                           .worldTick = snapshot->worldTick,
                           .acknowledgedCommand = 0,
                           .entities = {},
                           .npcsWhole = snapshot->npcsWhole};
  for (const coop::EntityState& entity : snapshot->entities) {
    world.entities.push_back(
        {.networkId = entity.networkId,
         .owner = entity.owner,
         .role = entity.role,
         .position = {static_cast<float>(entity.x), static_cast<float>(entity.y),
                      static_cast<float>(entity.z)},
         .yaw = static_cast<float>(entity.yaw) * 2.0F * std::numbers::pi_v<float> / 65536.0F,
         .motion = entity.motion});
  }
  return world;
}

float apart(glm::vec3 a, glm::vec3 b) {
  return glm::distance(glm::vec2{a.x, a.z}, glm::vec2{b.x, b.z});
}

} // namespace

TEST_CASE("co-op NPCs: the host's snapshot lists its map NPCs after the players, by slot") {
  Side host;
  const auto snapshot = makeCoopSnapshot(host.field, "room", 1, 0);
  REQUIRE(snapshot);
  REQUIRE(snapshot->entities.size() == 4);
  CHECK(snapshot->entities[2].networkId == coop::NpcNetworkBase);
  CHECK(snapshot->entities[3].networkId == coop::NpcNetworkBase + 1);
  CHECK(snapshot->entities[3].role == coop::EntityRole::Npc);
  CHECK(snapshot->entities[3].owner == coop::HostPlayerId);
  CHECK(snapshot->entities[3].x == -800);
  CHECK(coop::decode(coop::encode(*snapshot)));
}

TEST_CASE("co-op NPCs: a guest's map NPCs walk where the host's do, not their own way") {
  Side host;
  Side guest;
  CoopNpcMirror mirror;
  host.run(2.0F);
  guest.run(0.5F);
  REQUIRE(apart(*host.field.npcs().positionAt(0), *guest.field.npcs().positionAt(0)) > 100.0F);

  const PresentedCoopWorld world = drawn(host.field);
  CHECK(mirror.show(guest.field.npcs(), {.world = &world, .mapSerial = 1, .ready = true},
                    Millis{0}) == 0);
  guest.run(1.0F);
  for (std::size_t slot = 0; slot < 2; ++slot) {
    CHECK(apart(*host.field.npcs().positionAt(slot), *guest.field.npcs().positionAt(slot)) < 1.0F);
  }

  CHECK(mirror.show(guest.field.npcs(), {.world = nullptr}, Millis{100}) == 0);
  CHECK_FALSE(guest.field.npcs().mirrored(0));
  const glm::vec3 left = *guest.field.npcs().positionAt(0);
  guest.run(1.0F);
  CHECK(apart(left, *guest.field.npcs().positionAt(0)) > 50.0F);
}

TEST_CASE("co-op NPCs: one the host took off leaves the guest's map a second later, no sooner") {
  Side host;
  Side guest;
  CoopNpcMirror mirror;
  REQUIRE(host.field.npcs().takeOffUntilReload(1));
  const PresentedCoopWorld world = drawn(host.field);
  const auto show = [&](Millis now, bool ready = true) {
    return mirror.show(guest.field.npcs(), {.world = &world, .mapSerial = 1, .ready = ready}, now);
  };

  // Still while a script of the guest's game runs: nothing counts.
  CHECK(show(Millis{0}, false) == 0);
  CHECK(show(Millis{2000}, false) == 0);
  CHECK(show(Millis{2100}) == 0);
  CHECK(show(Millis{2900}) == 0);
  CHECK(guest.field.npcs().entityAt(1).has_value());
  CHECK(show(Millis{3100}) == 1);
  CHECK_FALSE(guest.field.npcs().entityAt(1).has_value());
  CHECK(guest.field.npcs().entityAt(0).has_value());

  // The host's last NPCs went (a battle won against all of them): the guest's go too (#410).
  Side empty;
  Side other;
  REQUIRE(empty.field.npcs().unloadAt(0));
  REQUIRE(empty.field.npcs().unloadAt(1));
  const PresentedCoopWorld none = drawn(empty.field);
  REQUIRE(none.npcsWhole);
  CoopNpcMirror fresh;
  CHECK(fresh.show(other.field.npcs(), {.world = &none, .mapSerial = 1, .ready = true},
                   Millis{0}) == 0);
  CHECK(fresh.show(other.field.npcs(), {.world = &none, .mapSerial = 1, .ready = true},
                   Millis{1100}) == 2);
  CHECK(other.field.npcs().poses().empty());

  // A snapshot that left its NPCs out to fit tells nothing of them.
  Side kept;
  PresentedCoopWorld partial = none;
  partial.npcsWhole = false;
  CoopNpcMirror careful;
  for (const Millis now : {Millis{0}, Millis{5000}}) {
    CHECK(careful.show(kept.field.npcs(), {.world = &partial, .mapSerial = 1, .ready = true},
                       now) == 0);
  }
  CHECK(kept.field.npcs().poses().size() == 2);
}

TEST_CASE("co-op NPCs: a battle of the guest's own keeps its NPCs where it put them") {
  Side host;
  Side guest;
  CoopNpcMirror mirror;
  host.run(2.0F);
  guest.run(0.5F);
  const glm::vec3 own = *guest.field.npcs().positionAt(0);
  REQUIRE(apart(*host.field.npcs().positionAt(0), own) > 100.0F);
  REQUIRE(host.field.npcs().takeOffUntilReload(1));
  const PresentedCoopWorld world = drawn(host.field);

  // The guest's solo battle runs on the host's map: its fighters are its own, none is taken off.
  for (const Millis now : {Millis{0}, Millis{3000}}) {
    CHECK(mirror.show(guest.field.npcs(),
                      {.world = &world, .mapSerial = 1, .ready = true, .battling = true},
                      now) == 0);
  }
  CHECK_FALSE(guest.field.npcs().mirrored(0));
  CHECK(apart(own, *guest.field.npcs().positionAt(0)) < 1.0F);
  CHECK(guest.field.npcs().entityAt(1).has_value());

  CHECK(mirror.show(guest.field.npcs(), {.world = &world, .mapSerial = 1, .ready = true},
                    Millis{3100}) == 0);
  CHECK(apart(*host.field.npcs().positionAt(0), *guest.field.npcs().positionAt(0)) < 1.0F);
}
