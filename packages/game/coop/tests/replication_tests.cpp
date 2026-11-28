#include <dwe/game/coop/authority.hpp>
#include <dwe/game/coop/protocol.hpp>

#include <doctest/doctest.h>

#include <string>
#include <variant>

#include "support/coop_pair.hpp"

// A player's partner form after it evolves and the clips a script plays on its characters.
using namespace dwe::game::coop;
using dwe::game::coop::test::CoopPair;

namespace {

void roundTrip(const Message& message) {
  const auto decoded = decode(encode(message));
  REQUIRE(decoded);
  REQUIRE(decoded->has_value());
  CHECK(**decoded == message);
}

} // namespace

TEST_CASE("coop replication: scripted clips, a held partner and a partner form round trip") {
  roundTrip(Snapshot{.mapId = "gym",
                     .worldTick = 9,
                     .acknowledgedCommand = 2,
                     .entities = {{.networkId = 1,
                                   .owner = HostPlayerId,
                                   .role = EntityRole::Player,
                                   .x = 10,
                                   .y = 0,
                                   .z = -20,
                                   .yaw = 300,
                                   .motion = Motion::Walk},
                                  {.networkId = 2,
                                   .owner = HostPlayerId,
                                   .role = EntityRole::Partner,
                                   .x = 700,
                                   .y = 0,
                                   .z = 600,
                                   .yaw = 9000,
                                   .motion = Motion::Idle,
                                   .action = ActionClip{.clip = 41, .starts = 255}}}});
  roundTrip(PlayerState{.player = GuestPlayerId,
                        .sequence = 30,
                        .epoch = 2,
                        .mapId = "gym",
                        .x = 1,
                        .y = 2,
                        .z = 3,
                        .yaw = 4,
                        .motion = Motion::Idle,
                        .action = ActionClip{.clip = 7, .starts = 1},
                        .partner = HeldPartner{.x = 700,
                                               .y = 0,
                                               .z = 600,
                                               .yaw = 16384,
                                               .motion = Motion::Walk,
                                               .action = ActionClip{.clip = 41, .starts = 2}}});
  roundTrip(PlayerState{
      .player = 3, .sequence = 1, .epoch = 1, .mapId = "gym", .partner = HeldPartner{.x = -5}});
  roundTrip(PartnerForm{.player = GuestPlayerId, .species = "greymon"});

  CHECK_FALSE(decode(encode(PartnerForm{.player = HostPlayerId, .species = "greymon"})));
  CHECK_FALSE(decode(encode(PartnerForm{.player = 1, .species = std::string(65, 'g')})));
  CHECK_FALSE(decode(encode(PlayerState{
      .player = 1, .mapId = "gym", .partner = HeldPartner{.x = MaxWorldCoordinate + 1}})));
  // A clip cut short is damaged.
  auto bytes = encode(
      Snapshot{.mapId = "gym",
               .entities = {{.networkId = 1, .action = ActionClip{.clip = 41, .starts = 1}}}});
  bytes.pop_back();
  CHECK_FALSE(decode(bytes));
}

TEST_CASE("coop replication: a guest's partner form reaches the host's roster, never the other "
          "way") {
  CoopPair pair;
  REQUIRE(pair.connect());
  (void)CoopPair::drain(pair.host());
  (void)CoopPair::drain(pair.guest());
  // Only the guest tells its own partner's form; the host's goes in its roster.
  CHECK_FALSE(pair.host().send(PartnerForm{.player = GuestPlayerId, .species = "greymon"}));
  pair.deliver(pair.guest(), PartnerForm{.player = GuestPlayerId, .species = "greymon"});
  const auto heard = CoopPair::drain(pair.host());
  REQUIRE(heard.size() == 1);
  const auto* form = std::get_if<PartnerForm>(&heard.front());
  REQUIRE(form != nullptr);

  // The authority lists the new form once, under a new roster revision.
  WorldAuthority authority{"Host", "agumon"};
  REQUIRE(authority.join("Guest", "agumon", dwe::net::Millis{0}) == GuestPlayerId);
  const std::uint32_t before = authority.revision();
  authority.setPartner(GuestPlayerId, form->species);
  authority.setPartner(GuestPlayerId, form->species);
  CHECK(authority.revision() == before + 1);
  const Roster roster = authority.roster();
  REQUIRE(roster.players.size() == 2);
  CHECK(roster.players[1].partnerSpecies == "greymon");
  CHECK(roster.players[0].partnerSpecies == "agumon");
}
