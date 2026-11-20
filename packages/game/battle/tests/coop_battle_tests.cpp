// Co-op battles (docs/architecture/coop-combat.md §4): another player's partner fights beside the
// partner, after the enemies in the fighter list.

#include <dwe/game/battle/battle.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "fixture.hpp"

using namespace battle_test;

namespace {

constexpr std::uint8_t GuestOwner = 1;

// The duel with a second partner beside the first, its player behind it.
BattleSetup coopDuel(CombatStats partner, CombatStats ally, CombatStats enemy) {
  BattleSetup setup = duel(partner, enemy);
  setup.allies.push_back(
      {.partner = fighterWith("buddy", ally, {"punch"}, {.x = 300, .z = 0}),
       .condition =
           {.discipline = 50, .tiredness = 0, .sick = false, .injured = false, .knownMoves = {}},
       .tamer = {.x = 300, .z = -800},
       .owner = GuestOwner});
  return setup;
}

// Steps until `done` or the frame limit.
template <typename Done>
void stepUntil(Battle& battle, Done done, std::int64_t limit = 20000) {
  while (!battle.finished() && !done() && battle.state().frame < limit) {
    battle.step();
  }
}

} // namespace

TEST_CASE("an ally fights after the enemies, on the partner's side") {
  Battle battle =
      startBattle(coopDuel(evenStats(300, 100), evenStats(300, 100), evenStats(300, 100)), 3);
  const BattleState& state = battle.state();
  REQUIRE(state.fighters.size() == 3);
  REQUIRE(state.allies.size() == 1);
  const FighterId ally = state.allies.front().id;
  CHECK(ally == 2);
  CHECK(state.enemyCount() == 1);
  CHECK(state.isEnemy(1));
  CHECK(state.isPlayerSide(PartnerId));
  CHECK(state.isPlayerSide(ally));
  // No friendly fire between the partners; the enemy is everyone's foe.
  CHECK_FALSE(state.foes(PartnerId, ally));
  CHECK_FALSE(state.foes(ally, PartnerId));
  CHECK(state.foes(ally, 1));
  CHECK(state.foes(1, ally));
  CHECK(state.allies.front().owner == GuestOwner);
}

TEST_CASE("a battle without allies is the original one") {
  BattleSetup setup = duel(evenStats(400, 300), evenStats(40, 20));
  Battle solo = startBattle(setup, 7);
  runToEnd(solo);
  CHECK(solo.allyResults().empty());
  CHECK(resultOf(solo).departure == Departure::None);
  CHECK(solo.state().enemyCount() == 1);
}

TEST_CASE("two partners win together: the bits stay in the battle's result") {
  Battle battle =
      startBattle(coopDuel(evenStats(400, 300), evenStats(400, 300), evenStats(60, 20)), 7);
  runToEnd(battle);
  REQUIRE(battle.finished());
  const BattleResult& result = resultOf(battle);
  CHECK(result.outcome == Outcome::Victory);
  CHECK(result.bits == 50);
  REQUIRE(battle.allyResults().size() == 1);
  const AllyResult& ally = battle.allyResults().front();
  CHECK(ally.owner == GuestOwner);
  CHECK(ally.result.outcome == Outcome::Victory);
  CHECK(ally.result.bits == 0);
  CHECK(ally.result.care.battles == 1);
  CHECK(ally.result.departure == Departure::None);
}

TEST_CASE("each player commands their own partner, and only theirs") {
  Battle battle =
      startBattle(coopDuel(evenStats(300, 100), evenStats(300, 100), evenStats(300, 100)), 4);
  const FighterId ally = battle.state().allies.front().id;
  CHECK(battle.command(Command::Attack, ally));
  // An enemy takes no command.
  CHECK_FALSE(battle.command(Command::Attack, 1));
  // A player who left: the partner fights on its own call.
  battle.releaseAlly(ally);
  CHECK_FALSE(battle.command(Command::Defend, ally));
}

TEST_CASE("an ally that runs off leaves the battle, which goes on for the partner") {
  Battle battle =
      startBattle(coopDuel(evenStats(400, 100), evenStats(400, 100), evenStats(400, 100)), 5);
  const FighterId ally = battle.state().allies.front().id;
  REQUIRE(battle.command(Command::Flee, ally));
  stepUntil(battle, [&] { return battle.state().fighters[ally].gone(); });
  REQUIRE_FALSE(battle.finished());
  CHECK(battle.state().fighters[ally].departure == Departure::Fled);
  battle.defeatEnemies();
  runToEnd(battle);
  REQUIRE(battle.finished());
  CHECK(resultOf(battle).outcome == Outcome::Victory);
  REQUIRE(battle.allyResults().size() == 1);
  const BattleResult& fled = battle.allyResults().front().result;
  CHECK(fled.departure == Departure::Fled);
  CHECK(fled.care.happiness == -10);
}

TEST_CASE("a partner knocked out while its ally stands is out with 1 HP after the win") {
  Battle battle =
      startBattle(coopDuel(evenStats(400, 100), evenStats(400, 300), evenStats(400, 100)), 6);
  battle.defeatPartner();
  stepUntil(battle, [&] { return battle.state().partner().gone(); });
  REQUIRE_FALSE(battle.finished());
  CHECK(battle.state().partner().departure == Departure::KnockedOut);
  battle.defeatEnemies();
  runToEnd(battle);
  REQUIRE(battle.finished());
  const BattleResult& result = resultOf(battle);
  CHECK(result.outcome == Outcome::Victory);
  CHECK(result.departure == Departure::KnockedOut);
  CHECK(result.partnerHp == 1);
  CHECK(result.care.happiness == -10);
  CHECK(result.livesLost == 0);
  CHECK(battle.allyResults().front().result.departure == Departure::None);
}

TEST_CASE("with every partner gone the team ran off, or lost when none ran") {
  Battle fled =
      startBattle(coopDuel(evenStats(400, 100), evenStats(400, 100), evenStats(400, 100)), 8);
  const FighterId ally = fled.state().allies.front().id;
  REQUIRE(fled.command(Command::Flee));
  REQUIRE(fled.command(Command::Flee, ally));
  runToEnd(fled);
  REQUIRE(fled.finished());
  CHECK(resultOf(fled).outcome == Outcome::Fled);
  CHECK(fled.allyResults().front().result.outcome == Outcome::Fled);

  Battle lost =
      startBattle(coopDuel(evenStats(400, 100), evenStats(400, 100), evenStats(400, 100)), 8);
  const FighterId other = lost.state().allies.front().id;
  lost.defeatPartner();
  stepUntil(lost, [&] { return lost.state().partner().gone(); });
  REQUIRE_FALSE(lost.finished());
  CHECK_FALSE(lost.state().foes(1, PartnerId));
  REQUIRE(lost.command(Command::Flee, other));
  runToEnd(lost);
  REQUIRE(lost.finished());
  CHECK(resultOf(lost).outcome == Outcome::Fled);
  CHECK(resultOf(lost).departure == Departure::KnockedOut);
}

TEST_CASE("allies have no place in a versus match") {
  BattleSetup setup = coopDuel(evenStats(300, 100), evenStats(300, 100), evenStats(300, 100));
  setup.kind = BattleKind::Versus;
  setup.versus = VersusSetup{};
  CHECK_FALSE(Battle::start(setup, originalRules(), testCatalog(), originalStrategies(), 1));
}
