#include <dwe/game/battle/folder/formulas.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "folder_kit.hpp"

using namespace folder_test;

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
