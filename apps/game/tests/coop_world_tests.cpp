// The co-op game's shared world on each side (#353 authority, #354 transforms, #355 lifecycle,
// #356 maps, #358 smoothing), against bare fields: the host's field with the guests' bodies, a
// guest's field with its own player.

#include <dwe/game/coop/battle.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/scene/world.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "coop_world.hpp"
#include "packages/game/field/tests/fixture.hpp"

using namespace dwe::app;
namespace coop = dwe::game::coop;
using dwe::game::field::Field;
using dwe::game::field::testing::originalRules;
using dwe::game::field::testing::roomMap;
using dwe::net::Millis;

namespace {

dwe::assets::SpawnPoint at(float x, float z) {
  return {.position = {x, 0.0F, z}, .yaw = 0.0F};
}

coop::PlayerState stateOf(coop::PlayerId id, std::uint32_t sequence, std::uint16_t epoch,
                          std::int32_t x, std::int32_t z, std::string map = "room") {
  return {.player = id,
          .sequence = sequence,
          .epoch = epoch,
          .mapId = std::move(map),
          .x = x,
          .y = 0,
          .z = z,
          .yaw = 0,
          .motion = coop::Motion::Run};
}

template <typename T>
std::vector<T> messagesOf(const std::vector<CoopOutgoing>& out, coop::PlayerId to) {
  std::vector<T> found;
  for (const CoopOutgoing& message : out) {
    if (message.to == to) {
      if (const auto* typed = std::get_if<T>(&message.message)) {
        found.push_back(*typed);
      }
    }
  }
  return found;
}

// A host on "room" with guest 1 in its field, joined and placed (its first correction taken).
struct HostSession {
  dwe::scene::World world;
  Field field{world, originalRules()};
  CoopHostWorld host{"Host", "agumon"};
  std::uint32_t serial = 1;
  coop::PlayerCorrection placed;

  HostSession() {
    field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
    REQUIRE(host.join("Guest", "gabumon", Millis{0}) == coop::GuestPlayerId);
    REQUIRE(field.addPlayer(coop::GuestPlayerId, at(500.0F, 0.0F)));
    const auto out = host.tick(field, serial, std::nullopt, Millis{0});
    const auto corrections = messagesOf<coop::PlayerCorrection>(out, coop::GuestPlayerId);
    REQUIRE(corrections.size() == 1);
    placed = corrections.front();
  }
};

} // namespace

TEST_CASE("a joining guest is placed at the host's free spot and told who is in the session") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  CoopHostWorld host{"Host", "agumon"};
  REQUIRE(host.join("Guest", "gabumon", Millis{0}) == coop::GuestPlayerId);
  REQUIRE(field.addPlayer(coop::GuestPlayerId, at(500.0F, 0.0F)));

  const auto out = host.tick(field, 1, std::nullopt, Millis{0});
  const auto corrections = messagesOf<coop::PlayerCorrection>(out, coop::GuestPlayerId);
  REQUIRE(corrections.size() == 1);
  CHECK(corrections.front().mapId == "room");
  CHECK(corrections.front().x == 500);
  CHECK(corrections.front().epoch == 1);
  const auto rosters = messagesOf<coop::Roster>(out, coop::GuestPlayerId);
  REQUIRE(rosters.size() == 1);
  REQUIRE(rosters.front().players.size() == 2);
  CHECK(rosters.front().players[1].partnerSpecies == "gabumon");
  // Nothing new, nothing sent.
  CHECK(host.tick(field, 1, std::nullopt, Millis{16}).empty());
  CHECK(host.authority().playersOn("room") ==
        std::vector<coop::PlayerId>{coop::HostPlayerId, coop::GuestPlayerId});
}

TEST_CASE("the host drives the guest's body from its valid states, smoothed and never stale") {
  HostSession session;
  // The guest runs along x from where it was put, a state every 50 ms.
  for (std::uint32_t i = 1; i <= 10; ++i) {
    session.host.receive(coop::GuestPlayerId,
                         stateOf(coop::GuestPlayerId, i * 3, 1, 500 + static_cast<int>(i * 50), 0),
                         Millis{i * 50});
  }
  // A late state and one from another map change nothing.
  session.host.receive(coop::GuestPlayerId, stateOf(coop::GuestPlayerId, 6, 1, -900, 0),
                       Millis{520});
  session.host.receive(coop::GuestPlayerId, stateOf(coop::GuestPlayerId, 33, 1, 1100, 0, "other"),
                       Millis{530});
  // The authority holds the newest valid state; the drawn body runs a little behind it (#358).
  CHECK(session.host.authority().find(coop::GuestPlayerId)->pose->x == 1000);
  (void)session.host.tick(session.field, session.serial, std::nullopt, Millis{525});
  session.field.tick(1.0F / 60.0F);
  const float drawn = session.field.player(coop::GuestPlayerId)->position.x;
  CHECK(drawn == doctest::Approx(925.0F).epsilon(0.01));
  CHECK(session.field.player(coop::GuestPlayerId)->motion == dwe::scene::Motion::Run);
  CHECK(session.host.authority().find(coop::GuestPlayerId)->pose->x == 1000);
  // Starved of states (a loss), it runs on a short way past the newest one and holds there.
  (void)session.host.tick(session.field, session.serial, std::nullopt, Millis{5000});
  session.field.tick(1.0F / 60.0F);
  const float held = session.field.player(coop::GuestPlayerId)->position.x;
  CHECK(held >= 1000.0F);
  CHECK(held <= 1100.0F);
  (void)session.host.tick(session.field, session.serial, std::nullopt, Millis{9000});
  session.field.tick(1.0F / 60.0F);
  CHECK(session.field.player(coop::GuestPlayerId)->position.x == doctest::Approx(held));
  // States again: the newest arrives where the guest is.
  session.host.receive(coop::GuestPlayerId, stateOf(coop::GuestPlayerId, 33, 1, 1050, 0),
                       Millis{9050});
  (void)session.host.tick(session.field, session.serial, std::nullopt, Millis{20000});
  session.field.tick(1.0F / 60.0F);
  CHECK(session.field.player(coop::GuestPlayerId)->position.x >= 1050.0F);
}

TEST_CASE("a guest that keeps sending impossible states is put back on both sides") {
  HostSession session;
  session.host.receive(coop::GuestPlayerId, stateOf(coop::GuestPlayerId, 3, 1, 550, 0), Millis{50});
  // Thousands of units in a few ticks: refused, three in a row.
  for (std::uint32_t i = 2; i <= 4; ++i) {
    session.host.receive(coop::GuestPlayerId, stateOf(coop::GuestPlayerId, i * 3, 1, 9000, 0),
                         Millis{i * 50});
  }
  const auto out = session.host.tick(session.field, session.serial, std::nullopt, Millis{200});
  const auto corrections = messagesOf<coop::PlayerCorrection>(out, coop::GuestPlayerId);
  REQUIRE(corrections.size() == 1);
  CHECK(corrections.front().x == 550);
  CHECK(corrections.front().epoch == 2);
  session.field.tick(1.0F / 60.0F);
  CHECK(session.field.player(coop::GuestPlayerId)->position.x == doctest::Approx(550.0F));
  // States from before the correction are dropped from now on.
  session.host.receive(coop::GuestPlayerId, stateOf(coop::GuestPlayerId, 15, 1, 560, 0),
                       Millis{250});
  CHECK(session.host.authority().find(coop::GuestPlayerId)->pose->x == 550);
}

TEST_CASE("a map change of the host takes every guest along, with the host's arrival") {
  HostSession session;
  session.host.receive(coop::GuestPlayerId, stateOf(coop::GuestPlayerId, 3, 1, 550, 0), Millis{50});
  session.field.enterMapAt(roomMap("next"), at(-300.0F, 0.0F));
  const auto out = session.host.tick(session.field, session.serial + 1, 2, Millis{100});
  const auto corrections = messagesOf<coop::PlayerCorrection>(out, coop::GuestPlayerId);
  REQUIRE(corrections.size() == 1);
  CHECK(corrections.front().mapId == "next");
  CHECK(corrections.front().exit == 2);
  CHECK(corrections.front().epoch == 2);
  const auto body = session.field.player(coop::GuestPlayerId);
  CHECK(corrections.front().x == static_cast<std::int32_t>(std::lround(body->position.x)));
  // The guest still on the old map: its states are refused, never applied to the new one (#356).
  session.host.receive(coop::GuestPlayerId, stateOf(coop::GuestPlayerId, 6, 1, 600, 0),
                       Millis{150});
  session.host.receive(coop::GuestPlayerId, stateOf(coop::GuestPlayerId, 9, 2, 600, 0, "room"),
                       Millis{160});
  (void)session.host.tick(session.field, session.serial + 1, 2, Millis{200});
  CHECK(session.field.player(coop::GuestPlayerId)->position == body->position);
  CHECK(session.host.authority().find(coop::GuestPlayerId)->pose->mapId == "next");
  CHECK(session.host.authority().playersOn("room").empty());
}

TEST_CASE("a guest that leaves goes from the session; the next one joins clean") {
  HostSession session;
  session.host.leave(coop::GuestPlayerId);
  CHECK(session.field.removePlayer(coop::GuestPlayerId));
  CHECK(session.host.authority().find(coop::GuestPlayerId) == nullptr);
  CHECK(session.host.authority().size() == 1);
  // Its states are nobody's now.
  session.host.receive(coop::GuestPlayerId, stateOf(coop::GuestPlayerId, 3, 1, 550, 0), Millis{50});
  CHECK(session.host.authority().find(coop::GuestPlayerId) == nullptr);
  CHECK(session.host.tick(session.field, session.serial, std::nullopt, Millis{60}).empty());
  // It comes back: the same id, a new record, placed again.
  REQUIRE(session.host.join("Guest", "gabumon", Millis{100}) == coop::GuestPlayerId);
  REQUIRE(session.field.addPlayer(coop::GuestPlayerId, at(500.0F, 0.0F)));
  const auto out = session.host.tick(session.field, session.serial, std::nullopt, Millis{120});
  CHECK(messagesOf<coop::PlayerCorrection>(out, coop::GuestPlayerId).size() == 1);
  CHECK(messagesOf<coop::Roster>(out, coop::GuestPlayerId).size() == 1);
  CHECK(session.host.authority().size() == 2);
}

TEST_CASE("three guests are each placed, driven and listed by their own id") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  CoopHostWorld host{"Host", ""};
  for (coop::PlayerId id = 1; id < coop::MaxPlayers; ++id) {
    REQUIRE(host.join("Guest", "", Millis{0}) == id);
    REQUIRE(field.addPlayer(id, at(600.0F * static_cast<float>(id), 0.0F)));
  }
  CHECK_FALSE(host.join("Fifth", "", Millis{0}));
  const auto out = host.tick(field, 1, std::nullopt, Millis{0});
  for (coop::PlayerId id = 1; id < coop::MaxPlayers; ++id) {
    const auto corrections = messagesOf<coop::PlayerCorrection>(out, id);
    REQUIRE(corrections.size() == 1);
    CHECK(corrections.front().player == id);
    CHECK(corrections.front().x == 600 * id);
    const auto rosters = messagesOf<coop::Roster>(out, id);
    REQUIRE(rosters.size() == 1);
    CHECK(rosters.front().players.size() == 4);
  }
  // Each one's states move its own body only; one guest cannot write another's.
  for (coop::PlayerId id = 1; id < coop::MaxPlayers; ++id) {
    host.receive(id, stateOf(id, 3, 1, (600 * id) + 40, 100), Millis{50});
  }
  host.receive(1, stateOf(2, 6, 1, 0, 0), Millis{60});
  (void)host.tick(field, 1, std::nullopt, Millis{5000});
  field.tick(1.0F / 60.0F);
  for (coop::PlayerId id = 1; id < coop::MaxPlayers; ++id) {
    CHECK(field.player(id)->position.x == doctest::Approx((600.0F * id) + 40.0F));
    CHECK(field.player(id)->position.z == doctest::Approx(100.0F));
  }
}

TEST_CASE("a guest follows the host's corrections and reports with their epoch") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  CoopGuestWorld guest{coop::GuestPlayerId};
  // Nothing to report before the host placed it.
  CHECK_FALSE(guest.report(field, false, 3));

  // Same map: a place at once, then the reports carry epoch 1.
  guest.receive(coop::PlayerCorrection{.player = coop::GuestPlayerId,
                                       .epoch = 1,
                                       .mapId = "room",
                                       .exit = std::nullopt,
                                       .x = 400,
                                       .y = 0,
                                       .z = -200,
                                       .yaw = 0});
  // Another player's correction is not this guest's.
  guest.receive(coop::PlayerCorrection{.player = 2,
                                       .epoch = 9,
                                       .mapId = "far",
                                       .exit = std::nullopt,
                                       .x = 0,
                                       .y = 0,
                                       .z = 0,
                                       .yaw = 0});
  const auto place = guest.apply(field, false);
  REQUIRE(place);
  CHECK(place->map == "room");
  CHECK(place->at.position == glm::vec3{400.0F, 0.0F, -200.0F});
  field.placeAt(field.playerEntity(), place->at.position, place->at.yaw);
  CHECK_FALSE(guest.apply(field, false));
  const auto state = guest.report(field, false, 6);
  REQUIRE(state);
  CHECK(state->epoch == 1);
  CHECK(state->x == 400);
  CHECK(state->mapId == "room");
  CHECK_FALSE(guest.report(field, false, 7));

  // Another map: a map change there, no reports on the way or from the old map (#356).
  guest.receive(coop::PlayerCorrection{.player = coop::GuestPlayerId,
                                       .epoch = 2,
                                       .mapId = "next",
                                       .exit = 1,
                                       .x = 100,
                                       .y = 0,
                                       .z = 0,
                                       .yaw = 0});
  // The zone notice comes first, and the move waits for its lead (#356 UX).
  for (std::uint32_t tick = 0; tick < CoopGuestWorld::ZoneLeadTicks; ++tick) {
    REQUIRE_FALSE(guest.apply(field, false));
    CHECK(guest.leadingToZone());
    CHECK_FALSE(guest.report(field, false, 9));
  }
  const auto zone = guest.takeZoneChange();
  REQUIRE(zone);
  CHECK(zone->map == "next");
  CHECK(zone->epoch == 2);
  CHECK_FALSE(guest.takeZoneChange());
  const auto change = guest.apply(field, false);
  REQUIRE(change);
  CHECK_FALSE(guest.leadingToZone());
  CHECK(change->map == "next");
  CHECK(change->exit == 1);
  CHECK_FALSE(guest.report(field, false, 9));
  CHECK_FALSE(guest.apply(field, true));
  CHECK_FALSE(guest.report(field, true, 12));
  field.enterMapAt(roomMap("next"), change->at);
  CHECK_FALSE(guest.apply(field, false));
  const auto arrived = guest.report(field, false, 15);
  REQUIRE(arrived);
  CHECK(arrived->mapId == "next");
  CHECK(arrived->epoch == 2);
}

TEST_CASE("a room the host's warp reset is loaded again by the guest, not kept as it was") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  CoopGuestWorld guest{coop::GuestPlayerId};
  const auto correction = [](std::uint16_t epoch, std::optional<std::int32_t> exit,
                             std::int32_t x) {
    return coop::PlayerCorrection{.player = coop::GuestPlayerId,
                                  .epoch = epoch,
                                  .mapId = "room",
                                  .exit = exit,
                                  .x = x,
                                  .y = 0,
                                  .z = 0,
                                  .yaw = 0};
  };
  // Joining on the map it already shows: a place, no load.
  guest.receive(correction(1, 0, 300));
  const auto joined = guest.apply(field, false);
  REQUIRE(joined);
  CHECK_FALSE(joined->reload);
  field.placeAt(field.playerEntity(), joined->at.position, joined->at.yaw);
  CHECK_FALSE(guest.apply(field, false));

  // The host's script warps it back onto the same map (a room reset): this game loads it too.
  guest.receive(correction(2, 0, 100));
  const auto reset = guest.apply(field, false);
  REQUIRE(reset);
  CHECK(reset->reload);
  CHECK(reset->map == "room");
  CHECK_FALSE(guest.report(field, false, 3));
  CHECK_FALSE(guest.apply(field, true));
  field.enterMapAt(roomMap("room"), reset->at);
  CHECK_FALSE(guest.apply(field, false));
  const auto state = guest.report(field, false, 6);
  REQUIRE(state);
  CHECK(state->epoch == 2);

  // A correction of refused states (no exit) only puts the player back.
  guest.receive(correction(3, std::nullopt, 400));
  const auto back = guest.apply(field, false);
  REQUIRE(back);
  CHECK_FALSE(back->reload);
  // A room reset is no change of zone.
  CHECK_FALSE(guest.takeZoneChange());
}

TEST_CASE("only the host's lead to another map tells the guest the zone changed (#356)") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  CoopGuestWorld guest{coop::GuestPlayerId};
  const auto correction = [](std::uint16_t epoch, std::string map,
                             std::optional<std::int32_t> exit) {
    return coop::PlayerCorrection{.player = coop::GuestPlayerId,
                                  .epoch = epoch,
                                  .mapId = std::move(map),
                                  .exit = exit,
                                  .x = 0,
                                  .y = 0,
                                  .z = 0,
                                  .yaw = 0};
  };
  // Joining on another map than the one it shows: no notice, no wait.
  guest.receive(correction(1, "room2", 0));
  REQUIRE(guest.apply(field, false));
  CHECK_FALSE(guest.takeZoneChange());
  field.enterMapAt(roomMap("room2"), at(0.0F, 0.0F));
  CHECK_FALSE(guest.apply(field, false));

  // The guest's own story warp took it elsewhere: the host brings it back to the group's map
  // without a notice.
  field.enterMapAt(roomMap("elsewhere"), at(0.0F, 0.0F));
  guest.receive(correction(2, "room2", std::nullopt));
  REQUIRE(guest.apply(field, false));
  CHECK_FALSE(guest.takeZoneChange());
  CHECK_FALSE(guest.leadingToZone());
  field.enterMapAt(roomMap("room2"), at(0.0F, 0.0F));
  CHECK_FALSE(guest.apply(field, false));

  // The host leads on while the guest's own map change runs: the lead waits for it, then counts.
  guest.receive(correction(3, "room3", 1));
  CHECK_FALSE(guest.apply(field, true));
  CHECK_FALSE(guest.takeZoneChange());
  for (std::uint32_t tick = 0; tick < CoopGuestWorld::ZoneLeadTicks; ++tick) {
    REQUIRE_FALSE(guest.apply(field, false));
  }
  REQUIRE(guest.takeZoneChange());
  // A newer lead during the wait neither restarts it nor tells the change twice.
  guest.receive(correction(4, "room4", 2));
  const auto move = guest.apply(field, false);
  REQUIRE(move);
  CHECK(move->map == "room4");
  CHECK_FALSE(guest.takeZoneChange());
  field.enterMapAt(roomMap("room4"), at(0.0F, 0.0F));
  CHECK_FALSE(guest.apply(field, false));

  // The host comes back during the lead: this game loads the map again as the host did, and the
  // next lead counts from the start.
  guest.receive(correction(5, "room5", 0));
  CHECK_FALSE(guest.apply(field, false));
  REQUIRE(guest.takeZoneChange());
  guest.receive(correction(6, "room4", 0));
  const auto again = guest.apply(field, false);
  REQUIRE(again);
  CHECK(again->reload);
  CHECK_FALSE(guest.leadingToZone());
  field.enterMapAt(roomMap("room4"), at(0.0F, 0.0F));
  CHECK_FALSE(guest.apply(field, false));
  guest.receive(correction(7, "room6", 0));
  CHECK_FALSE(guest.apply(field, false));
  CHECK(guest.takeZoneChange());
}

TEST_CASE("a guest keeps the newest roster and knows who is in the session") {
  CoopGuestWorld guest{coop::GuestPlayerId};
  CHECK(guest.inSession(coop::HostPlayerId));
  guest.receive(coop::Roster{.revision = 3,
                             .players = {{.id = 0, .name = "Host", .partnerSpecies = "agumon"},
                                         {.id = 1, .name = "Me", .partnerSpecies = "gabumon"},
                                         {.id = 2, .name = "Other", .partnerSpecies = "patamon"}}});
  CHECK(guest.inSession(2));
  CHECK(guest.partnerOf(2) == "patamon");
  // An older roster arriving late changes nothing; a newer one drops the player that left.
  guest.receive(coop::Roster{.revision = 2,
                             .players = {{.id = 0, .name = "Host", .partnerSpecies = "agumon"}}});
  CHECK(guest.inSession(2));
  guest.receive(coop::Roster{.revision = 4,
                             .players = {{.id = 0, .name = "Host", .partnerSpecies = "agumon"},
                                         {.id = 1, .name = "Me", .partnerSpecies = "gabumon"}}});
  CHECK_FALSE(guest.inSession(2));
  CHECK(guest.partnerOf(2).empty());
}

TEST_CASE("a guest keeps another guest's bodies that left while its partner still fights (#352)") {
  CoopGuestWorld guest{3};
  coop::BattleMirror mirror{3};
  mirror.receive(coop::BattleBegin{.battle = 5,
                                   .starter = 2,
                                   .ask = 9,
                                   .mapId = "forest",
                                   .enemyNpcs = {1},
                                   .species = {"patamon", "foe", "agumon", "gabumon"},
                                   .fixedFight = false,
                                   .allies = {coop::HostPlayerId, 3},
                                   .folder = false});
  CHECK(mirror.fighting() == std::vector<coop::PlayerId>{2, coop::HostPlayerId, 3});
  // Player 2 started the battle and left: the host's roster drops it, its partner fights on.
  guest.receive(coop::Roster{.revision = 1,
                             .players = {{.id = 0, .name = "Host", .partnerSpecies = "agumon"},
                                         {.id = 1, .name = "Other", .partnerSpecies = "elecmon"},
                                         {.id = 3, .name = "Me", .partnerSpecies = "gabumon"}}});
  CHECK_FALSE(guest.inSession(2));
  CHECK(guest.keepsBodies(2, mirror.fighting()));
  CHECK(guest.keepsBodies(1, mirror.fighting()));
  // Once the battle ends nothing holds them.
  mirror.receive(coop::BattleEnd{.battle = 5,
                                 .outcome = coop::BattleOutcome::Victory,
                                 .leftMap = {},
                                 .bits = 0,
                                 .spoils = std::nullopt});
  CHECK(mirror.fighting().empty());
  CHECK_FALSE(guest.keepsBodies(2, mirror.fighting()));
}

TEST_CASE("shared battles see a player here only on this map and not between maps (#351)") {
  HostSession session;
  // The host: its authority puts the guest on the host's map.
  const auto here = session.host.authority().playersOn(session.field.mapId());
  CHECK(coopPlayerHere(session.field, here, coop::GuestPlayerId));
  CHECK(coopPlayerHere(session.field, here, coop::HostPlayerId));
  CHECK_FALSE(coopPlayerHere(session.field, here, 2));
  // A list for another map, or a field on its way to one, has nobody here.
  CHECK_FALSE(coopPlayerHere(session.field, session.host.authority().playersOn("next"),
                             coop::GuestPlayerId));
  session.field.startScriptedMapChange(
      {.map = "next", .exit = 0, .trigger = 0, .arrivalSection = std::nullopt}, true);
  CHECK_FALSE(coopPlayerHere(session.field, here, coop::GuestPlayerId));
}
