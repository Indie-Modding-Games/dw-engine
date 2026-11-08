#include <dwe/game/battle/battle.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/sections.hpp>
#include <dwe/game/versus/link.hpp>
#include <dwe/net/loopback.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/battle_registry.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/versus_match.hpp>
#include <dwe/session/versus_series.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "packages/game/battle/tests/fixture.hpp"
#include "versus_fixture.hpp"

using dwe::game::battle::Battle;
using dwe::game::battle::BattleResult;
using dwe::game::battle::Command;
using dwe::game::battle::Outcome;
using dwe::game::battle::PartnerId;
using dwe::game::battle::RivalId;
using dwe::game::versus::LinkPhase;
using dwe::game::versus::RoundResult;
using dwe::game::versus::SeriesSetup;
using dwe::game::versus::VersusFighter;
using dwe::game::versus::VersusLink;
using dwe::net::LoopbackNetwork;
using dwe::net::Millis;
using dwe::session::RegisteredPartner;
using dwe::session::VersusPlacement;
using dwe::session::VersusSeries;
using dwe::session::testing::errorOf;
using dwe::session::testing::TestPacks;
using versus_test::Machine;
using versus_test::registered;

namespace {

constexpr Millis Frame{50};

VersusFighter fighterOf(RegisteredPartner partner) {
  return dwe::session::versusFighterOf(partner);
}

const VersusPlacement Arena{.map = "",
                            .host = {.x = 0, .z = -300},
                            .guest = {.x = 0, .z = 300},
                            .bystander = {.x = 0, .z = -2000}};

// What a player presses: now and then one of its menu's commands, picked from the frame.
std::uint32_t inputAt(const Battle& battle, dwe::game::battle::FighterId side, std::int64_t frame) {
  const std::vector<Command>& menu = battle.menuOf(side);
  if (menu.empty() || frame % 37 != 0) {
    return 0;
  }
  return dwe::session::versusInputOf(menu.at(static_cast<std::size_t>(frame / 37) % menu.size()));
}

} // namespace

TEST_CASE("main_menu.c registerBattleData: a registered partner travels as its record") {
  const RegisteredPartner partner = registered("pal", "first_kind", 300, 70);
  const VersusFighter fighter = dwe::session::versusFighterOf(partner);
  CHECK(fighter.name == "pal");
  CHECK(fighter.species == "first_kind");
  CHECK(fighter.brains == 300);
  CHECK(fighter.discipline == 70);
  CHECK(fighter.techniques == partner.techniques);
}

TEST_CASE("vs_scene.c VS_loadFighterEntities: the record at full HP and MP with its finisher") {
  const TestPacks packs;
  Machine machine{packs};
  auto combatant =
      machine.battle->versusCombatant(fighterOf(registered("pal", "first_kind", 300, 70)));
  REQUIRE_MESSAGE(combatant, errorOf(combatant));
  CHECK(combatant->stats.maxHp == 400);
  CHECK(combatant->hp == 400);
  CHECK(combatant->mp == 300);
  CHECK(combatant->stats.brain == 300);
  CHECK(combatant->moves.at(0) == "punch");
  CHECK(combatant->moves.at(1) == "shot");
  CHECK_FALSE(combatant->moves.at(2));
  // VS_getFirstSpecialMove: the species' finisher, whatever the record has.
  CHECK(combatant->moves.at(dwe::game::battle::FinisherSlot) == "big_finish");

  VersusFighter stranger = fighterOf(registered("odd", "no_such_kind", 300, 70));
  CHECK_FALSE(machine.battle->versusCombatant(stranger));
  VersusFighter badMove = fighterOf(registered("odd", "first_kind", 300, 70));
  badMove.techniques.at(2) = "no_such_move";
  CHECK_FALSE(machine.battle->versusCombatant(badMove));
  VersusFighter tooStrong = fighterOf(registered("odd", "first_kind", 300, 70));
  tooStrong.offense = 5000;
  CHECK_FALSE(machine.battle->versusCombatant(tooStrong));
  tooStrong.offense = 100;
  tooStrong.hp = 0;
  CHECK_FALSE(machine.battle->versusCombatant(tooStrong));
}

TEST_CASE("a versus battle: host 1P against guest 2P, each with its brain's menu and discipline") {
  const TestPacks packs;
  Machine machine{packs};
  auto battle =
      machine.battle->startVersus(fighterOf(registered("one", "first_kind", 300, 90)),
                                  fighterOf(registered("two", "second_kind", 600, 20)), Arena, 11);
  REQUIRE_MESSAGE(battle, errorOf(battle));
  CHECK(battle->state().fighters.size() == 2);
  REQUIRE(battle->state().versus());
  CHECK(battle->state().rival->discipline == 20);
  CHECK(battle->state().matchClock->seconds == 99);
  CHECK(battle->menuOf(PartnerId) == std::vector{Command::Finisher, Command::Distance,
                                                 Command::Moderate, Command::Attack,
                                                 Command::YourCall});
  CHECK(std::ranges::find(battle->menuOf(RivalId), Command::Flee) == battle->menuOf(RivalId).end());
  dwe::session::commandVersus(*battle, {dwe::session::versusInputOf(Command::Attack),
                                        dwe::session::versusInputOf(Command::Defend)});
  CHECK(battle->state().commands.buffered == Command::Attack);
  CHECK(battle->state().rival->commands.buffered == Command::Defend);
}

TEST_CASE("vs_combat.c VS__combatMain: a battle's end as the series counts it") {
  BattleResult won;
  won.outcome = Outcome::Victory;
  CHECK(dwe::session::roundResultOf(won) == RoundResult::HostWon);
  BattleResult lost;
  lost.outcome = Outcome::Defeat;
  CHECK(dwe::session::roundResultOf(lost) == RoundResult::GuestWon);
  lost.draw = true;
  CHECK(dwe::session::roundResultOf(lost) == RoundResult::BothLost);
  CHECK(dwe::session::versusCommandOf(0x1'0004) == Command::Moderate);
  CHECK(dwe::session::versusCommandOf(0xEE) == Command::None);
  CHECK(dwe::session::roundSeed(SeriesSetup{.battles = 3, .arena = 0, .seed = 40, .inputDelay = 2},
                                2) == 42);
}

TEST_CASE("the content hash is the same for the same content and differs for another") {
  const TestPacks packs;
  Machine one{packs};
  Machine two{packs};
  auto first = dwe::session::versusContentHash(one.session->rules().current(), packs.files());
  auto second = dwe::session::versusContentHash(two.session->rules().current(), packs.files());
  REQUIRE_MESSAGE(first, errorOf(first));
  REQUIRE_MESSAGE(second, errorOf(second));
  CHECK(first->size() == 40);
  CHECK(*first == *second);
  const TestPacks otherTable{{"data/moves.yaml", "moves: []\n# another edition\n"}};
  Machine three{otherTable};
  auto third =
      dwe::session::versusContentHash(three.session->rules().current(), otherTable.files());
  REQUIRE_MESSAGE(third, errorOf(third));
  CHECK(*third != *first);
}

TEST_CASE("a whole versus series between two machines over a bad link plays the same battles") {
  const TestPacks packs;
  Machine hostMachine{packs};
  Machine guestMachine{packs};
  auto identity =
      dwe::session::versusIdentity("0.1.0", hostMachine.session->rules().current(), packs.files());
  REQUIRE_MESSAGE(identity, errorOf(identity));
  LoopbackNetwork network{{.latency = Millis{30}, .jitter = Millis{40}, .lossPercent = 10}, 9};
  const SeriesSetup setup{.battles = 3, .arena = 0, .seed = 1234, .inputDelay = 3};
  VersusSeries host{*hostMachine.battle, VersusLink::host(network.peer(0), *identity, setup),
                    Arena};
  VersusSeries guest{*guestMachine.battle, VersusLink::guest(network.peer(1), *identity), Arena};
  CHECK(host.localFighter() == PartnerId);
  CHECK(guest.localFighter() == RivalId);
  const auto run = [&](int frames) {
    for (int frame = 0; frame < frames; ++frame) {
      host.poll(network.now());
      guest.poll(network.now());
      network.advance(Frame);
    }
  };
  run(20);
  REQUIRE(host.link().phase() == LinkPhase::Lineups);
  CHECK_FALSE(host.startNext());
  REQUIRE(host.link().sendLineup({fighterOf(registered("a", "first_kind", 300, 90)),
                                  fighterOf(registered("b", "second_kind", 150, 40)),
                                  fighterOf(registered("c", "first_kind", 600, 70))}));
  REQUIRE(guest.link().sendLineup({fighterOf(registered("x", "second_kind", 500, 60)),
                                   fighterOf(registered("y", "first_kind", 250, 100)),
                                   fighterOf(registered("z", "second_kind", 50, 10))}));
  run(20);
  REQUIRE(host.link().phase() == LinkPhase::Playing);
  std::int32_t battles = 0;
  // Three battles of at most 99 seconds and their ends, at a frame a tick.
  for (std::int64_t frame = 0; frame < 9000 && host.link().phase() == LinkPhase::Playing; ++frame) {
    for (VersusSeries* side : {&host, &guest}) {
      if (side->battle() == nullptr || side->roundSettled()) {
        if (side->startNext() && side == &host) {
          ++battles;
        }
      }
      if (const Battle* battle = side->battle()) {
        if (const std::uint32_t input = inputAt(*battle, side->localFighter(), frame)) {
          side->command(dwe::session::versusCommandOf(input));
        }
      }
      (void)side->frame();
    }
    run(1);
  }
  run(20);
  INFO("host failure ", static_cast<int>(host.link().failure()), ", guest failure ",
       static_cast<int>(guest.link().failure()));
  CHECK(battles == 3);
  CHECK(host.link().phase() == LinkPhase::Finished);
  CHECK(guest.link().phase() == LinkPhase::Finished);
  CHECK(host.link().series().results() == guest.link().series().results());
  CHECK(host.link().series().results().size() == 3);
  CHECK(host.battle()->checksum() == guest.battle()->checksum());
}

TEST_CASE("a save's registered partners are read without starting its game") {
  dwe::session::BattleRegistry registry;
  REQUIRE(registry.registerAt(4, registered("late", "second_kind", 100, 50)));
  REQUIRE(registry.registerAt(1, registered("early", "first_kind", 200, 60)));
  dwe::game::save::SaveSections sections;
  REQUIRE(registry.addSaveSections(sections));
  dwe::game::save::SaveFile file;
  REQUIRE(sections.write(file));
  auto partners = dwe::session::registeredPartnersIn(file);
  REQUIRE_MESSAGE(partners, errorOf(partners));
  REQUIRE(partners->size() == 2);
  CHECK(partners->at(0).name == "early");
  CHECK(partners->at(1) == registered("late", "second_kind", 100, 50));
  // A save from before the registry has none.
  auto none = dwe::session::registeredPartnersIn(dwe::game::save::SaveFile{});
  REQUIRE_MESSAGE(none, errorOf(none));
  CHECK(none->empty());
}
