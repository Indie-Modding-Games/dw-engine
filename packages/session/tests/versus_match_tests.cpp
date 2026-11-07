#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/versus/link.hpp>
#include <dwe/net/loopback.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/battle_registry.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/versus_match.hpp>

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

using dwe::assets::DigimonRow;
using dwe::assets::DigimonTable;
using dwe::game::battle::Battle;
using dwe::game::battle::BattleKind;
using dwe::game::battle::BattleResult;
using dwe::game::battle::Command;
using dwe::game::battle::Outcome;
using dwe::game::battle::PartnerId;
using dwe::game::battle::RivalId;
using dwe::game::versus::LinkPhase;
using dwe::game::versus::RoundResult;
using dwe::game::versus::SeriesSetup;
using dwe::game::versus::Side;
using dwe::game::versus::VersusFighter;
using dwe::game::versus::VersusLink;
using dwe::net::LinkConditions;
using dwe::net::LoopbackNetwork;
using dwe::net::Millis;
using dwe::session::BattleAdapter;
using dwe::session::BattleAdapterDesc;
using dwe::session::GameSession;
using dwe::session::RegisteredPartner;
using dwe::session::SessionSystem;
using dwe::session::VersusPlacement;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

constexpr Millis Frame{50};

DigimonRow madeUpRow(std::string id, std::int32_t index, std::vector<std::string> moves) {
  DigimonRow row;
  row.id = std::move(id);
  row.index = index;
  row.name = row.id;
  row.radius = 100;
  row.type = 2;
  row.level = 3;
  row.special = {1, std::nullopt, std::nullopt};
  for (std::size_t slot = 0; slot < moves.size(); ++slot) {
    if (!moves[slot].empty()) {
      row.moves.at(slot) = moves[slot];
    }
  }
  return row;
}

// The new game partner the session needs, and two made-up species for the registries.
DigimonTable madeUpSpecies() {
  std::vector<DigimonRow> rows;
  rows.push_back(madeUpRow("agum", 8, {"punch", "shot", "", "", "big_finish"}));
  rows.push_back(madeUpRow("first_kind", 20, {"punch", "shot", "dynamite_kick", "", "big_finish"}));
  rows.push_back(madeUpRow("second_kind", 21, {"punch", "shot", "", "big_finish"}));
  return DigimonTable{std::move(rows)};
}

// The adapter's description with the made-up tables.
BattleAdapterDesc madeUpTables(const TestPacks& packs) {
  BattleAdapterDesc desc{&packs.files(), madeUpSpecies(), battle_test::testMoveTable(), nullptr};
  return desc;
}

// One player's machine: its own session and battle adapter over the same packs.
struct Machine {
  explicit Machine(const TestPacks& packs) {
    auto adapter = std::make_unique<BattleAdapter>(madeUpTables(packs));
    battle = adapter.get();
    std::vector<std::unique_ptr<SessionSystem>> systems;
    systems.push_back(std::move(adapter));
    session = newGameWith(packs, std::move(systems));
  }

  std::unique_ptr<GameSession> session;
  BattleAdapter* battle = nullptr;
};

RegisteredPartner registered(std::string name, std::string species, std::int32_t brains,
                             std::int32_t discipline) {
  return {.name = std::move(name),
          .species = std::move(species),
          .hp = 400,
          .mp = 300,
          .offense = 120,
          .defense = 60,
          .speed = 100,
          .brains = brains,
          .discipline = discipline,
          .techniques = {std::string{"punch"}, std::string{"shot"}, std::nullopt}};
}

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

// A side of the series: the link and the battle that runs on its machine.
struct Player {
  Machine* machine;
  VersusLink link;
  std::optional<Battle> battle;
  std::int64_t frame = 0;

  void startIfDue() {
    if (link.phase() != LinkPhase::Playing || battle || link.lockstep() != nullptr) {
      return;
    }
    REQUIRE(link.startRound());
    const std::int32_t round = link.series().round();
    const VersusFighter& host = link.lineup(Side::Host)->at(static_cast<std::size_t>(round));
    const VersusFighter& guest = link.lineup(Side::Guest)->at(static_cast<std::size_t>(round));
    auto started = machine->battle->startVersus(host, guest, Arena,
                                                dwe::session::roundSeed(*link.setup(), round));
    REQUIRE_MESSAGE(started, errorOf(started));
    battle.emplace(std::move(*started));
  }

  void playFrame() {
    startIfDue();
    if (!battle || link.lockstep() == nullptr || link.awaitingResult()) {
      return;
    }
    const auto side = link.side() == Side::Host ? PartnerId : RivalId;
    link.submitInput(inputAt(*battle, side, frame++));
    while (link.lockstep() != nullptr && !link.awaitingResult()) {
      const auto inputs = link.lockstep()->ready();
      if (!inputs) {
        break;
      }
      dwe::session::commandVersus(*battle, *inputs);
      battle->step();
      link.playedTick(battle->checksum());
      if (battle->finished()) {
        link.finishRound(dwe::session::roundResultOf(*battle->result()), battle->checksum());
      }
    }
  }

  // Once both agreed on the battle's result, the next one may start.
  void settle() {
    if (battle && battle->finished() && !link.awaitingResult()) {
      battle.reset();
    }
  }
};

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
  Player host{.machine = &hostMachine,
              .link = VersusLink::host(network.peer(0), *identity, setup),
              .battle = std::nullopt};
  Player guest{.machine = &guestMachine,
               .link = VersusLink::guest(network.peer(1), *identity),
               .battle = std::nullopt};
  const auto run = [&](int frames) {
    for (int frame = 0; frame < frames; ++frame) {
      host.link.poll(network.now());
      guest.link.poll(network.now());
      network.advance(Frame);
    }
  };
  run(20);
  REQUIRE(host.link.phase() == LinkPhase::Lineups);
  REQUIRE(host.link.sendLineup({fighterOf(registered("a", "first_kind", 300, 90)),
                                fighterOf(registered("b", "second_kind", 150, 40)),
                                fighterOf(registered("c", "first_kind", 600, 70))}));
  REQUIRE(guest.link.sendLineup({fighterOf(registered("x", "second_kind", 500, 60)),
                                 fighterOf(registered("y", "first_kind", 250, 100)),
                                 fighterOf(registered("z", "second_kind", 50, 10))}));
  run(20);
  REQUIRE(host.link.phase() == LinkPhase::Playing);
  // Three battles of at most 99 seconds and their ends, at a frame a tick.
  for (int frame = 0; frame < 9000 && host.link.phase() == LinkPhase::Playing; ++frame) {
    host.playFrame();
    guest.playFrame();
    run(1);
    host.settle();
    guest.settle();
  }
  run(20);
  INFO("host failure ", static_cast<int>(host.link.failure()), ", guest failure ",
       static_cast<int>(guest.link.failure()));
  CHECK(host.link.phase() == LinkPhase::Finished);
  CHECK(guest.link.phase() == LinkPhase::Finished);
  CHECK(host.link.series().results() == guest.link.series().results());
  CHECK(host.link.series().results().size() == 3);
}
