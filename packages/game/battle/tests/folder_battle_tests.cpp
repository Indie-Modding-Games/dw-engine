#include <dwe/game/battle/folder_formulas.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "fixture.hpp"

using namespace dwe::game::battle;
using namespace battle_test;

namespace {

const FolderRules& folderRules() {
  REQUIRE(originalRules().folder.has_value());
  return *originalRules().folder;
}

BattleSetup folderDuel(CombatStats partner, CombatStats enemy,
                       std::vector<std::string> partnerMoves = {"punch", "shot"},
                       std::vector<std::string> enemyMoves = {"punch"}) {
  BattleSetup setup = duel(partner, enemy, std::move(partnerMoves), std::move(enemyMoves));
  setup.system = BattleSystemId::Folder;
  return setup;
}

BattleEvents runFrames(Battle& battle, std::int64_t frames) {
  BattleEvents all;
  for (std::int64_t frame = 0; frame < frames && !battle.finished(); ++frame) {
    const BattleEvents& step = battle.step();
    all.insert(all.end(), step.begin(), step.end());
  }
  return all;
}

std::vector<BattleEvent> eventsOf(const BattleEvents& events, EventKind kind, FighterId fighter) {
  std::vector<BattleEvent> found;
  std::ranges::copy_if(events, std::back_inserter(found), [&](const BattleEvent& event) {
    return event.kind == kind && event.fighter == fighter;
  });
  return found;
}

std::vector<OrderPhase> phasesOf(const BattleEvents& events, FighterId fighter) {
  std::vector<OrderPhase> phases;
  for (const BattleEvent& event : eventsOf(events, EventKind::OrderPhaseChanged, fighter)) {
    phases.push_back(static_cast<OrderPhase>(event.amount));
  }
  return phases;
}

} // namespace

TEST_CASE("the folder formulas follow the ruleset's numbers") {
  const FolderRules& rules = folderRules();
  CHECK(folder::movementPercent(rules, 0) == 75);
  CHECK(folder::movementPercent(rules, 500) == 100);
  CHECK(folder::movementPercent(rules, 1000) == 125);
  CHECK(folder::movementPercent(rules, 2000) == 125);
  CHECK(folder::executionFrames(rules, 10, 0) == 10);
  CHECK(folder::executionFrames(rules, 10, 1000) == 6);
  CHECK(folder::processingFrames(rules, 16, 1000) == 8);
  CHECK(folder::processingFrames(rules, 16, 500) == 12);

  const folder::Specials fire{0, std::nullopt, std::nullopt};
  const folder::Specials earth{3, std::nullopt, std::nullopt};
  const folder::Specials air{2, std::nullopt, std::nullopt};
  const folder::Specials none{};
  // Fire beats earth, air beats fire; fire on a fire user adds STAB.
  CHECK(folder::elementPercent(rules, 0, none, earth) == 20);
  CHECK(folder::elementPercent(rules, 0, none, air) == -20);
  CHECK(folder::elementPercent(rules, 0, fire, earth) == 40);
  CHECK(folder::elementPercent(rules, 0, none, none) == 0);

  CHECK(folder::typePercent(rules, DigimonType::Vaccine, DigimonType::Virus) == 25);
  CHECK(folder::typePercent(rules, DigimonType::Virus, DigimonType::Vaccine) == -25);
  CHECK(folder::typePercent(rules, DigimonType::None, DigimonType::Data) == 0);

  CHECK(folder::damage(rules, {.power = 66, .attack = 100, .modifierPercent = 20, .defense = 50}) ==
        66 + 2 * 100 + 13 - 50);
  CHECK(folder::damage(rules, {.power = 10, .attack = 0, .modifierPercent = 0, .defense = 500}) ==
        1);
  CHECK(folder::damage(
            rules,
            {.power = 10, .attack = 0, .modifierPercent = 0, .defense = 500, .covered = true}) ==
        0);
  CHECK(folder::damage(
            rules,
            {.power = 100, .attack = 100, .modifierPercent = 0, .defense = 50, .covered = true}) ==
        100 + 2 * 100 - 2 * 50);

  CHECK(folder::coverPercent(rules, 1000) == 30);
  CHECK(folder::coverPercent(rules, 500) == 15);
  CHECK(folder::barCapacity(rules, 100) == 600);
  CHECK(folder::barCapacity(rules, 0) == 1);
  CHECK(folder::statCutPercent(rules, 1000, 1000) == 30);
  CHECK(folder::statCutPercent(rules, 0, 0) == 0);
  CHECK(folder::breakFrames(rules, 0, 0) == 80);
  CHECK(folder::breakFrames(rules, 1000, 1000) == 56);
  CHECK(folder::barDrain(rules, 600, 0, 0) == 5);
  CHECK(folder::brokenStat(rules, 300) == 150);
}

TEST_CASE("the battle system names read back") {
  CHECK(battleSystemName(BattleSystemId::Classic) == "classic");
  CHECK(battleSystemName(BattleSystemId::Folder) == "folder");
  CHECK(battleSystemOf("folder") == BattleSystemId::Folder);
  CHECK(battleSystemOf("classic") == BattleSystemId::Classic);
  CHECK_FALSE(battleSystemOf("other").has_value());
}

TEST_CASE("a classic battle takes no orders and keeps no folder state") {
  Battle battle = startBattle(duel(evenStats(300, 100), evenStats(300, 100)), 1);
  CHECK(battle.system() == BattleSystemId::Classic);
  CHECK_FALSE(battle.state().folder.has_value());
  CHECK(battle.folderOf(PartnerId) == nullptr);
  CHECK(battle.defenseBarCapacity(PartnerId) == 0);
  CHECK_FALSE(battle.order(0, std::nullopt));
  CHECK_FALSE(battle.setStance(Stance::Hold));
}

TEST_CASE("a folder battle's strip gives orders and stances") {
  Battle battle = startBattle(folderDuel(evenStats(300, 100), evenStats(300, 100)), 1);
  CHECK(battle.system() == BattleSystemId::Folder);
  REQUIRE(battle.folderOf(PartnerId) != nullptr);
  CHECK(battle.defenseBarCapacity(PartnerId) == 600);
  const CommandState& commands = battle.state().commands;
  CHECK(commands.menu == std::vector<Command>{Command::Finisher, Command::Flee, Command::Change,
                                              Command::Defend, Command::Distance, Command::Moderate,
                                              Command::Attack, Command::Technique2,
                                              Command::Technique1, Command::YourCall});
  CHECK(commands.current == Command::Attack);
  REQUIRE(battle.command(Command::Defend));
  CHECK(battle.folderOf(PartnerId)->stance == Stance::Guard);
  CHECK(battle.state().commands.current == Command::Defend);
  REQUIRE(battle.command(Command::Technique2));
  REQUIRE(battle.folderOf(PartnerId)->queue.size() == 1);
  CHECK(battle.folderOf(PartnerId)->queue[0].slot == 1);
  REQUIRE(battle.command(Command::YourCall));
  CHECK(battle.folderOf(PartnerId)->queue.empty());
  CHECK_FALSE(battle.command(Command::Finisher));
  CHECK(battle.order(0, 1));
  CHECK_FALSE(battle.order(2, 1));
  CHECK_FALSE(battle.order(0, PartnerId + 7));
  CHECK(battle.command(Command::Flee));
}

TEST_CASE("the order queue holds two and the newest replaces the last waiting one") {
  Battle battle = startBattle(folderDuel(evenStats(300, 100), evenStats(300, 100)), 1);
  REQUIRE(battle.order(0, 1));
  REQUIRE(battle.order(1, 1));
  REQUIRE(battle.order(1, std::nullopt));
  const FolderFighter& own = *battle.folderOf(PartnerId);
  REQUIRE(own.queue.size() == OrderQueueSize);
  CHECK(own.queue[0].slot == 0);
  CHECK(own.queue[1].id == 3);
  CHECK_FALSE(own.queue[1].target.has_value());
  CHECK(own.lastEnd == OrderEnd::Replaced);
  CHECK(battle.cancelOrders());
  CHECK(battle.folderOf(PartnerId)->queue.empty());
}

TEST_CASE("without orders the partner only moves by its stance and never attacks") {
  Battle battle = startBattle(folderDuel(evenStats(300, 100), evenStats(300, 100)), 3);
  const BattleEvents events = runFrames(battle, 300);
  CHECK(eventsOf(events, EventKind::AttackStarted, PartnerId).empty());
  CHECK(battle.state().fighters.at(1).hp == 300);
}

TEST_CASE("an order runs its whole cycle and hits the ordered target") {
  Battle battle = startBattle(folderDuel(evenStats(300, 200), evenStats(1500, 50)), 5);
  REQUIRE(battle.setStance(Stance::Hold));
  REQUIRE(battle.order(1, 1));
  BattleEvents events = runFrames(battle, 400);
  const std::vector<OrderPhase> phases = phasesOf(events, PartnerId);
  REQUIRE(phases.size() >= 5);
  CHECK(phases[0] == OrderPhase::Processing);
  CHECK(phases[1] == OrderPhase::Positioning);
  CHECK(phases[2] == OrderPhase::Preparing);
  CHECK(phases[3] == OrderPhase::Executing);
  CHECK(phases[4] == OrderPhase::Recovering);
  const std::vector<BattleEvent> started = eventsOf(events, EventKind::AttackStarted, PartnerId);
  REQUIRE_FALSE(started.empty());
  CHECK(started.front().move == refOf(testCatalog(), "shot"));
  CHECK(started.front().other == FighterId{1});
  CHECK(started.size() == 1);
  CHECK_FALSE(eventsOf(events, EventKind::OrderDone, PartnerId).empty());
  CHECK(battle.folderOf(PartnerId)->phase == OrderPhase::Idle);
}

TEST_CASE("an order the partner cannot pay is invalid and says why") {
  BattleSetup setup = folderDuel(evenStats(300, 100), evenStats(300, 100));
  setup.partner.mp = 0;
  Battle battle = startBattle(std::move(setup), 1);
  CHECK(battle.orderProblem(1) == OrderProblem::NotEnoughMp);
  CHECK(battle.orderProblem(2) == OrderProblem::EmptySlot);
  REQUIRE(battle.order(1, 1));
  const BattleEvents events = runFrames(battle, 60);
  const std::vector<BattleEvent> invalid = eventsOf(events, EventKind::OrderInvalid, PartnerId);
  REQUIRE(invalid.size() == 1);
  CHECK(invalid.front().amount == static_cast<std::int32_t>(OrderProblem::NotEnoughMp));
  CHECK(eventsOf(events, EventKind::AttackStarted, PartnerId).empty());
}

TEST_CASE("the guard stance covers every hit, and the bar breaks the defense") {
  Battle battle = startBattle(folderDuel(evenStats(2000, 40), evenStats(2000, 300)), 7);
  REQUIRE(battle.setStance(Stance::Guard));
  const BattleEvents events = runFrames(battle, 2000);
  CHECK(eventsOf(events, EventKind::AttackHit, PartnerId).size() <=
        eventsOf(events, EventKind::DefenseBroken, PartnerId).size() * 8);
  CHECK_FALSE(eventsOf(events, EventKind::AttackBlocked, PartnerId).empty());
  CHECK_FALSE(eventsOf(events, EventKind::DefenseBroken, PartnerId).empty());
}

TEST_CASE("a versus match cannot be a folder battle") {
  BattleSetup setup = folderDuel(evenStats(300, 100), evenStats(300, 100));
  setup.versus = VersusSetup{.rivalDiscipline = 40};
  CHECK_FALSE(
      Battle::start(std::move(setup), originalRules(), testCatalog(), originalStrategies(), 1));
}

TEST_CASE("the folder battle is deterministic") {
  const auto run = [] {
    Battle battle = startBattle(folderDuel(evenStats(400, 150), evenStats(400, 120)), 11);
    std::vector<std::uint64_t> sums;
    for (std::size_t round = 0; round < 6; ++round) {
      (void)battle.order(round % 2, 1);
      (void)runFrames(battle, 80);
      sums.push_back(battle.checksum());
    }
    return sums;
  };
  CHECK(run() == run());
}

TEST_CASE("an aggressive partner under attack still lands its order") {
  Battle battle = startBattle(folderDuel(evenStats(800, 100), evenStats(800, 100)), 5);
  REQUIRE(battle.order(1, 1));
  const BattleEvents events = runFrames(battle, 600);
  CHECK_FALSE(eventsOf(events, EventKind::AttackStarted, PartnerId).empty());
  CHECK(eventsOf(events, EventKind::OrderInvalid, PartnerId).empty());
  CHECK(std::ranges::count_if(events, [](const BattleEvent& event) {
          return event.fighter == PartnerId && event.kind == EventKind::OrderPhaseChanged &&
                 static_cast<OrderPhase>(event.amount) == OrderPhase::Processing;
        }) == 1);
}

TEST_CASE("a technique's timing comes from its own entry, the finisher's or its MP tier") {
  FolderRules rules = folderRules();
  BattleMove move;
  move.id = "cheap";
  move.mpCost = 10;
  CHECK(folder::timingOf(rules, move) ==
        TechniqueTiming{.preparationFrames = 6, .recoveryFrames = 14});
  move.mpCost = 40;
  CHECK(folder::timingOf(rules, move) ==
        TechniqueTiming{.preparationFrames = 10, .recoveryFrames = 20});
  move.mpCost = 140;
  CHECK(folder::timingOf(rules, move) ==
        TechniqueTiming{.preparationFrames = 18, .recoveryFrames = 32});
  move.finisher = true;
  CHECK(folder::timingOf(rules, move) == rules.finisherTiming);
  rules.techniques.emplace("cheap", TechniqueTiming{.preparationFrames = 3, .recoveryFrames = 4});
  CHECK(folder::timingOf(rules, move) ==
        TechniqueTiming{.preparationFrames = 3, .recoveryFrames = 4});
  rules.timingTiers.clear();
  move.id = "other";
  move.finisher = false;
  move.mpCost = 40;
  CHECK(folder::timingOf(rules, move) ==
        TechniqueTiming{.preparationFrames = rules.preparationFrames,
                        .recoveryFrames = rules.recoveryFrames});
}

TEST_CASE("each strip order keeps the foe Change picked when it was given") {
  BattleSetup setup = folderDuel(evenStats(300, 100), evenStats(300, 100));
  setup.enemies.push_back(fighterWith("foe2", evenStats(300, 100), {"punch"}, {.x = 600, .z = 0}));
  Battle battle = startBattle(std::move(setup), 1);
  REQUIRE(battle.state().fighters[PartnerId].target == FighterId{1});
  REQUIRE(battle.command(Command::Change));
  REQUIRE(battle.state().fighters[PartnerId].target == FighterId{2});
  REQUIRE(battle.command(Command::Technique1));
  REQUIRE(battle.command(Command::Change));
  REQUIRE(battle.command(Command::Technique2));
  const std::vector<Order>& queue = battle.folderOf(PartnerId)->queue;
  REQUIRE(queue.size() == 2);
  CHECK(queue[0].target == FighterId{2});
  CHECK(queue[1].target == FighterId{1});
}

TEST_CASE("a folder partner under attack keeps the foe it was told to face") {
  BattleSetup setup = folderDuel(evenStats(2000, 100), evenStats(2000, 400));
  setup.enemies.push_back(fighterWith("foe2", evenStats(2000, 100), {""}, {.x = 2000, .z = 0}));
  Battle battle = startBattle(std::move(setup), 3);
  REQUIRE(battle.command(Command::Change));
  REQUIRE(battle.state().fighters[PartnerId].target == FighterId{2});
  bool hit = false;
  for (int frame = 0; frame < 600 && !battle.finished(); ++frame) {
    for (const BattleEvent& event : battle.step()) {
      hit = hit || (event.kind == EventKind::AttackHit && event.fighter == PartnerId);
    }
    CHECK(battle.state().fighters[PartnerId].target == FighterId{2});
  }
  CHECK(hit);
}

// Not a check: the numbers the provisional Folder rules are tuned with (folder-battle.md, "Pending
// balance"). Run with -tc='*balance probe*' --no-skip.
TEST_CASE("folder balance probe" * doctest::skip()) {
  struct Matchup {
    const char* name;
    CombatStats partner;
    CombatStats enemy;
  };
  const std::vector<Matchup> matchups = {
      {"even 300/100", evenStats(300, 100), evenStats(300, 100)},
      {"slightly weaker", evenStats(300, 100), evenStats(330, 115)},
      {"weaker", evenStats(300, 100), evenStats(400, 150)},
      {"stronger", evenStats(400, 150), evenStats(300, 100)},
      {"even 600/200", evenStats(600, 200), evenStats(600, 200)},
  };
  constexpr std::int64_t Seeds = 40;
  struct Variant {
    std::string name;
    BattleSystemId system;
    BattleRules rules;
  };
  std::vector<Variant> variants{{"classic", BattleSystemId::Classic, originalRules()}};
  for (const int percent : {100, 150, 200, 300}) {
    variants.push_back({"folder atk " + std::to_string(percent), BattleSystemId::Folder,
                        rulesOf(originalWith("attack_percent: 100",
                                             "attack_percent: " + std::to_string(percent)))});
  }
  for (const Matchup& matchup : matchups) {
    for (const Variant& variant : variants) {
      std::int64_t wins = 0;
      std::int64_t frames = 0;
      std::int64_t hpLeft = 0;
      for (std::int64_t seed = 1; seed <= Seeds; ++seed) {
        BattleSetup setup =
            duel(matchup.partner, matchup.enemy, {"punch", "shot"}, {"punch", "shot"});
        setup.system = variant.system;
        Battle battle =
            startBattle(std::move(setup), static_cast<std::uint64_t>(seed), variant.rules);
        while (!battle.finished() && battle.state().frame < 20000) {
          if (const FolderFighter* own = battle.folderOf(PartnerId);
              own && !own->active && own->queue.empty()) {
            const bool shot = battle.state().fighters[PartnerId].mp >= 10;
            static_cast<void>(battle.order(shot ? 1 : 0, 1));
          }
          battle.step();
        }
        if (const std::optional<BattleResult>& result = battle.result()) {
          wins += result->outcome == Outcome::Victory ? 1 : 0;
          frames += result->frames;
          hpLeft += result->partnerHp;
        } else {
          frames += battle.state().frame;
        }
      }
      MESSAGE(std::string{matchup.name} << " " << variant.name << ": wins " << wins * 100 / Seeds
                                        << "%, frames " << frames / Seeds << ", partner hp left "
                                        << hpLeft / Seeds);
    }
  }
}
