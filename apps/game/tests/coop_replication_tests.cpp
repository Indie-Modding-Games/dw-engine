// What each player's own game does to its partner that the others' games must show the same: its
// form after it evolves (#412), what a gym course plays on it and where (#420), and where it stands
// (#411).

#include <dwe/game/field/field.hpp>
#include <dwe/scene/world.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "coop_machine_spot.hpp"
#include "coop_snapshot.hpp"
#include "coop_world.hpp"
#include "packages/game/coop/tests/support/coop_pair.hpp"
#include "packages/game/field/tests/fixture.hpp"

using namespace dwe::app;
namespace coop = dwe::game::coop;
using dwe::game::field::Field;
using dwe::game::field::PlayedClip;
using dwe::game::field::testing::originalRules;
using dwe::game::field::testing::roomMap;
using dwe::net::Millis;

namespace {

dwe::assets::SpawnPoint at(float x, float z) {
  return {.position = {x, 0.0F, z}, .yaw = 0.0F};
}

template <typename T>
std::vector<T> messagesOf(const std::vector<CoopOutgoing>& out, coop::PlayerId to) {
  std::vector<T> found;
  for (const CoopOutgoing& message : out) {
    if (const auto* typed = std::get_if<T>(&message.message); typed && message.to == to) {
      found.push_back(*typed);
    }
  }
  return found;
}

std::string partnerIn(const coop::Roster& roster, coop::PlayerId id) {
  const auto found = std::ranges::find(roster.players, id, &coop::RosterEntry::id);
  return found != roster.players.end() ? found->partnerSpecies : std::string{};
}

// A host on "room" with guest 1 and its partner in its field, joined and placed.
struct HostSession {
  dwe::scene::World world;
  Field field{world, originalRules()};
  CoopHostWorld host{"Host", ""};
  coop::PlayerCorrection placed;
  coop::Roster roster;

  HostSession() {
    field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
    REQUIRE(host.join("Guest", "agumon", Millis{0}) == coop::GuestPlayerId);
    REQUIRE(field.addPlayer(coop::GuestPlayerId, at(500.0F, 0.0F)));
    REQUIRE(field.addPartner(coop::GuestPlayerId, "models/guest_partner", 120.0F));
    const auto out = host.tick(field, 1, std::nullopt, Millis{0});
    const auto corrections = messagesOf<coop::PlayerCorrection>(out, coop::GuestPlayerId);
    REQUIRE(corrections.size() == 1);
    placed = corrections.front();
    const auto rosters = messagesOf<coop::Roster>(out, coop::GuestPlayerId);
    REQUIRE(rosters.size() == 1);
    roster = rosters.front();
  }
};

// A guest's field on "room", placed by the host's first correction.
struct GuestSession {
  dwe::scene::World world;
  Field field{world, originalRules()};
  CoopGuestWorld guest{coop::GuestPlayerId};

  explicit GuestSession(const coop::PlayerCorrection& placed) {
    field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
    guest.receive(placed);
    if (const auto move = guest.apply(field, false)) {
      field.placeAt(field.playerEntity(), move->at.position, move->at.yaw);
    }
    REQUIRE_FALSE(guest.apply(field, false));
  }
};

} // namespace

TEST_CASE("co-op evolution: the host's new form reaches every guest's roster once") {
  HostSession session;
  session.host.setPartner(coop::HostPlayerId, "agumon");
  auto rosters = messagesOf<coop::Roster>(
      session.host.tick(session.field, 1, std::nullopt, Millis{16}), coop::GuestPlayerId);
  REQUIRE(rosters.size() == 1);
  CHECK(partnerIn(rosters.front(), coop::HostPlayerId) == "agumon");

  // It evolves: one roster with the new form, and nothing more while it stays so.
  session.host.setPartner(coop::HostPlayerId, "greymon");
  rosters = messagesOf<coop::Roster>(session.host.tick(session.field, 1, std::nullopt, Millis{33}),
                                     coop::GuestPlayerId);
  REQUIRE(rosters.size() == 1);
  CHECK(partnerIn(rosters.front(), coop::HostPlayerId) == "greymon");
  CHECK(partnerIn(rosters.front(), coop::GuestPlayerId) == "agumon");
  session.host.setPartner(coop::HostPlayerId, "greymon");
  CHECK(messagesOf<coop::Roster>(session.host.tick(session.field, 1, std::nullopt, Millis{50}),
                                 coop::GuestPlayerId)
            .empty());

  GuestSession guest{session.placed};
  guest.guest.receive(rosters.front());
  CHECK(guest.guest.partnerOf(coop::HostPlayerId) == "greymon");
}

TEST_CASE("co-op evolution: a guest's new form is told to the host until its roster lists it") {
  HostSession session;
  GuestSession guest{session.placed};
  // No roster yet: nothing to compare with.
  CHECK_FALSE(guest.guest.partnerForm("agumon", 1));
  guest.guest.receive(session.roster);
  // The roster already says so.
  CHECK_FALSE(guest.guest.partnerForm("agumon", 2));

  // It evolves on the guest's machine: the form goes once, and again a second later while the
  // host's roster still lists the old one (a form lost before the host's play read the link).
  const auto form = guest.guest.partnerForm("greymon", 3);
  REQUIRE(form);
  CHECK(form->player == coop::GuestPlayerId);
  CHECK(form->species == "greymon");
  CHECK_FALSE(guest.guest.partnerForm("greymon", 30));
  REQUIRE(guest.guest.partnerForm("greymon", 63));

  // The host takes it for the link's player, whatever the message names.
  session.host.receive(coop::GuestPlayerId,
                       coop::PartnerForm{.player = 3, .species = form->species}, Millis{40});
  CHECK(session.host.authority().find(coop::GuestPlayerId)->partnerSpecies == "greymon");
  const auto rosters = messagesOf<coop::Roster>(
      session.host.tick(session.field, 1, std::nullopt, Millis{50}), coop::GuestPlayerId);
  REQUIRE(rosters.size() == 1);
  CHECK(partnerIn(rosters.front(), coop::GuestPlayerId) == "greymon");
  guest.guest.receive(rosters.front());
  CHECK_FALSE(guest.guest.partnerForm("greymon", 200));
}

TEST_CASE("co-op training: a guest's course shows on the host where and as the guest plays it") {
  HostSession session;
  GuestSession guest{session.placed};
  // The guest's gym course holds its field, puts its partner on the equipment and plays a push.
  const glm::vec3 equipment{900.0F, 0.0F, 700.0F};
  guest.field.holdForScript(true);
  guest.field.placeByScript(guest.field.partnerEntity(), equipment);
  guest.field.animateByScript(guest.field.partnerEntity(), 41);
  guest.field.animateByScript(guest.field.playerEntity(), 5);
  const auto state = guest.guest.report(guest.field, false, 3);
  REQUIRE(state);
  REQUIRE(state->partner);
  CHECK(state->partner->x == 900);
  CHECK(state->partner->z == 700);
  REQUIRE(state->partner->action);
  CHECK(state->partner->action->clip == 41);
  CHECK(state->partner->action->starts == 1);
  REQUIRE(state->action);
  CHECK(state->action->clip == 5);

  // The host's copy of the guest's partner stands there and plays it, instead of following.
  session.host.receive(coop::GuestPlayerId, *state, Millis{50});
  (void)session.host.tick(session.field, 1, std::nullopt, Millis{200});
  session.field.tick(1.0F / 60.0F);
  const auto partner = session.field.partner(coop::GuestPlayerId);
  REQUIRE(partner);
  CHECK(glm::distance(partner->position, equipment) < 1.0F);
  const auto entity = *session.field.partnerEntity(coop::GuestPlayerId);
  CHECK(session.field.scriptedClipOf(entity) == PlayedClip{.clip = 41, .starts = 1});
  CHECK(session.field.scriptedClipOf(*session.field.playerEntity(coop::GuestPlayerId)) ==
        PlayedClip{.clip = 5, .starts = 1});

  // The other guests get it in the host's snapshot, the host's own course too.
  session.field.holdForScript(true);
  session.field.animateByScript(session.field.partnerEntity(), 43);
  const auto snapshot = makeCoopSnapshot(session.field, "room", 7, 0);
  REQUIRE(snapshot);
  const auto actionOf = [&](std::uint16_t id) {
    const auto found = std::ranges::find(snapshot->entities, id, &coop::EntityState::networkId);
    REQUIRE(found != snapshot->entities.end());
    return found->action;
  };
  CHECK(actionOf(1) == std::nullopt);
  CHECK(actionOf(2) == coop::ActionClip{.clip = 43, .starts = 1});
  CHECK(actionOf(3) == coop::ActionClip{.clip = 5, .starts = 1});
  CHECK(actionOf(4) == coop::ActionClip{.clip = 41, .starts = 1});

  // The course ends on the guest: no clip left, and the partner follows its player again.
  guest.field.releaseClipByScript(guest.field.partnerEntity());
  guest.field.releaseClipByScript(guest.field.playerEntity());
  guest.field.holdForScript(false);
  const auto after = guest.guest.report(guest.field, false, 6);
  REQUIRE(after);
  REQUIRE(after->partner);
  CHECK_FALSE(after->partner->action);
  CHECK_FALSE(after->action);
  session.host.receive(coop::GuestPlayerId, *after, Millis{100});
  (void)session.host.tick(session.field, 1, std::nullopt, Millis{250});
  CHECK_FALSE(session.field.scriptedClipOf(entity));
  CHECK_FALSE(session.field.scriptedClipOf(*session.field.playerEntity(coop::GuestPlayerId)));
}

TEST_CASE("co-op training: a second partner on the same machine is drawn beside the first (T8)") {
  HostSession session;
  // The host's own partner trains on the equipment.
  const glm::vec3 equipment{600.0F, 0.0F, 300.0F};
  session.field.holdForScript(true);
  session.field.placeByScript(session.field.partnerEntity(), equipment);
  session.field.animateByScript(session.field.partnerEntity(), 41);
  // The guest's game runs the same course on its own copy of the map: the same spot.
  coop::PlayerState state{
      .player = coop::GuestPlayerId,
      .sequence = 3,
      .epoch = session.placed.epoch,
      .mapId = "room",
      .x = session.placed.x,
      .y = 0,
      .z = session.placed.z,
      .partner = coop::HeldPartner{.x = 600, .z = 300, .action = coop::ActionClip{.clip = 41}}};
  session.host.receive(coop::GuestPlayerId, state, Millis{50});
  (void)session.host.tick(session.field, 1, std::nullopt, Millis{200});
  session.field.tick(1.0F / 60.0F);
  const auto partner = session.field.partner(coop::GuestPlayerId);
  REQUIRE(partner);
  const float apart = glm::distance(partner->position, equipment);
  CHECK(apart >= MachineSpotStep - 1.0F);
  CHECK(apart <= MachineSpotStep + 1.0F);

  // Off its clip, the guest's partner stands where its game has it, as before.
  state.sequence = 4;
  state.partner->action.reset();
  session.host.receive(coop::GuestPlayerId, state, Millis{100});
  (void)session.host.tick(session.field, 1, std::nullopt, Millis{400});
  session.field.tick(1.0F / 60.0F);
  const auto following = session.field.partner(coop::GuestPlayerId);
  REQUIRE(following);
  CHECK(glm::distance(following->position, equipment) < 1.0F);
}

TEST_CASE("co-op training: the spot beside a used machine is free, to either side") {
  const glm::vec3 machine{0.0F, 0.0F, 0.0F};
  const std::vector<glm::vec3> none;
  CHECK(machineSpotBeside(machine, 0.0F, none) == machine);
  // Facing +z, the first spot is one step to +x, the second one step to -x.
  const std::vector<glm::vec3> one{machine};
  const glm::vec3 first = machineSpotBeside(machine, 0.0F, one);
  CHECK(std::abs(first.x - MachineSpotStep) < 0.01F);
  CHECK(std::abs(first.z) < 0.01F);
  const std::vector<glm::vec3> two{machine, first};
  const glm::vec3 second = machineSpotBeside(machine, 0.0F, two);
  CHECK(std::abs(second.x + MachineSpotStep) < 0.01F);
  // A partner far enough away takes nothing.
  const std::vector<glm::vec3> far{{MachineSpotStep * 2.0F, 0.0F, 0.0F}};
  CHECK(machineSpotBeside(machine, 0.0F, far) == machine);
}

TEST_CASE("co-op training: a held partner reported far from its player is not drawn there") {
  HostSession session;
  coop::PlayerState state{.player = coop::GuestPlayerId,
                          .sequence = 3,
                          .epoch = session.placed.epoch,
                          .mapId = "room",
                          .x = session.placed.x,
                          .y = 0,
                          .z = session.placed.z,
                          .partner = coop::HeldPartner{.x = session.placed.x + 5000,
                                                       .z = session.placed.z,
                                                       .action = coop::ActionClip{.clip = 41}}};
  session.host.receive(coop::GuestPlayerId, state, Millis{50});
  (void)session.host.tick(session.field, 1, std::nullopt, Millis{200});
  session.field.tick(1.0F / 60.0F);
  const auto partner = session.field.partner(coop::GuestPlayerId);
  REQUIRE(partner);
  CHECK(partner->position.x < static_cast<float>(session.placed.x) + 1000.0F);
  CHECK_FALSE(session.field.scriptedClipOf(*session.field.partnerEntity(coop::GuestPlayerId)));
}

TEST_CASE("co-op partners: the host draws a guest's partner where the guest's game has it (#411)") {
  HostSession session;
  GuestSession guest{session.placed};
  coop::test::CoopPair pair;
  REQUIRE(pair.connect());
  (void)coop::test::CoopPair::drain(pair.host());
  (void)coop::test::CoopPair::drain(pair.guest());
  // The guest's states travel over the link, 50 ms apart, as its game reports them.
  std::uint64_t tick = 0;
  const auto report = [&] {
    tick += 3;
    const auto state = guest.guest.report(guest.field, false, tick);
    REQUIRE(state);
    pair.deliver(pair.guest(), *state, 50);
    for (const coop::Message& message : coop::test::CoopPair::drain(pair.host())) {
      session.host.receive(coop::GuestPlayerId, message, pair.now());
    }
  };
  const auto hostCopy = [&] {
    (void)session.host.tick(session.field, 1, std::nullopt, pair.now() + Millis{200});
    session.field.tick(1.0F / 60.0F);
    const auto partner = session.field.partner(coop::GuestPlayerId);
    REQUIRE(partner);
    return partner->position;
  };

  // The guest's partner stands where its own game moved it, unheld, away from where a follow of
  // the host's would put it.
  const glm::vec3 wandered{static_cast<float>(session.placed.x) + 300.0F, 0.0F, 450.0F};
  guest.field.placeAt(guest.field.partnerEntity(), wandered, 1.0F);
  for (int i = 0; i < 6; ++i) {
    report();
  }
  const auto state = guest.guest.report(guest.field, false, tick + 3);
  REQUIRE(state);
  REQUIRE(state->partner);
  CHECK_FALSE(state->partner->action);
  // The host's copy stands there: a battle starting now places the guest's partner where its
  // player sees it, and the host's snapshot shows it there to every other guest.
  CHECK(glm::distance(hostCopy(), wandered) < 1.0F);
  CHECK_FALSE(session.field.scriptedClipOf(*session.field.partnerEntity(coop::GuestPlayerId)));
  const auto snapshot = makeCoopSnapshot(session.field, "room", 9, 0);
  REQUIRE(snapshot);
  const auto copy = std::ranges::find(snapshot->entities, 4, &coop::EntityState::networkId);
  REQUIRE(copy != snapshot->entities.end());
  CHECK(std::abs(static_cast<float>(copy->x) - wandered.x) <= 1.0F);
  CHECK(std::abs(static_cast<float>(copy->z) - wandered.z) <= 1.0F);

  // It walks on in the guest's game: the host's copy goes along, drawn between the states.
  const glm::vec3 next = wandered + glm::vec3{0.0F, 0.0F, 200.0F};
  guest.field.placeAt(guest.field.partnerEntity(), next, 1.0F);
  for (int i = 0; i < 6; ++i) {
    report();
  }
  CHECK(glm::distance(hostCopy(), next) < 1.0F);
}
