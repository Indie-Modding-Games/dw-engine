#include <dwe/game/battle/battle.hpp>

#include <doctest/doctest.h>

#include <cstdint>

#include "coop_battle_view.hpp"
#include "packages/game/battle/tests/fixture.hpp"

using namespace battle_test;

namespace {

constexpr std::uint8_t GuestOwner = 2;

BattleSetup withAlly(BattleSetup setup) {
  setup.allies.push_back(
      {.partner = fighterWith("buddy", evenStats(400, 100), {"punch"}, {.x = 300, .z = 0}),
       .condition =
           {.discipline = 50, .tiredness = 0, .sick = false, .injured = false, .knownMoves = {}},
       .tamer = {.x = 300, .z = -800},
       .owner = GuestOwner});
  return setup;
}

} // namespace

TEST_CASE("an ally whose player left the battle runs off and the battle goes on without it") {
  Battle battle = startBattle(withAlly(duel(evenStats(400, 100), evenStats(400, 100))), 5);
  const FighterId ally = battle.state().allies.front().id;
  REQUIRE(battle.openBag(ally));
  dwe::app::withdrawCoopAlly(battle, ally);
  CHECK_FALSE(battle.bagOpen(ally));
  CHECK_FALSE(battle.command(Command::Attack, ally));
  while (!battle.finished() && !battle.state().fighters[ally].gone() &&
         battle.state().frame < 20000) {
    battle.step();
  }
  REQUIRE_FALSE(battle.finished());
  CHECK(battle.state().fighters[ally].departure == Departure::Fled);
  battle.defeatEnemies();
  runToEnd(battle);
  REQUIRE(battle.allyResults().size() == 1);
  CHECK(battle.allyResults().front().result.departure == Departure::Fled);
  CHECK(resultOf(battle).outcome == Outcome::Victory);
}

TEST_CASE("a story battle's ally whose player left fights on its own call") {
  BattleSetup setup = withAlly(duel(evenStats(400, 100), evenStats(400, 100)));
  setup.scripted = true;
  Battle battle = startBattle(std::move(setup), 5);
  const FighterId ally = battle.state().allies.front().id;
  dwe::app::withdrawCoopAlly(battle, ally);
  CHECK(battle.state().allyOf(ally)->released);
  CHECK_FALSE(battle.state().fleeFramesOf(ally).has_value());
}
