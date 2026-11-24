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

TEST_CASE("the guard stance covers every hit, and the bar breaks the defense") {
  Battle battle = startBattle(folderDuel(evenStats(2000, 40), evenStats(2000, 300)), 7);
  REQUIRE(battle.setStance(Stance::Guard));
  const BattleEvents events = runFrames(battle, 2000);
  CHECK(eventsOf(events, EventKind::AttackHit, PartnerId).size() <=
        eventsOf(events, EventKind::DefenseBroken, PartnerId).size() * 8);
  CHECK_FALSE(eventsOf(events, EventKind::AttackBlocked, PartnerId).empty());
  CHECK_FALSE(eventsOf(events, EventKind::DefenseBroken, PartnerId).empty());
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
