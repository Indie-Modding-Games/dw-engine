// Co-op battles (docs/architecture/coop-combat.md §4): another player's partner fights beside the
// partner, after the enemies in the fighter list.

#include <dwe/game/battle/battle.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "../fixture.hpp"

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

// docs/architecture/coop-difficulty.md §5: the enemies grow with the partners that fight.
TEST_CASE("co-op scaling follows the ruleset's tables and leaves single player alone") {
  const BattleRules& rules = originalRules();
  REQUIRE(rules.coopScaling.has_value());
  // One partner and nobody helping: the original, whatever the level.
  for (const CoopLevel level : {CoopLevel::Off, CoopLevel::Standard, CoopLevel::Hard}) {
    CHECK(coopScalingFactors(rules, level, 1, 0, false) == ScalingFactors{});
    CHECK(coopScalingFactors(rules, level, 1, 0, true) == ScalingFactors{});
  }
  // Off: the original numbers even with four partners.
  CHECK(coopScalingFactors(rules, CoopLevel::Off, 4, 0, false) == ScalingFactors{});
  CHECK(coopScalingFactors(rules, CoopLevel::Standard, 2, 0, false) ==
        ScalingFactors{.hp = 180, .mp = 150, .offense = 100, .finisherFill = 125});
  CHECK(coopScalingFactors(rules, CoopLevel::Standard, 3, 0, false) ==
        ScalingFactors{.hp = 250, .mp = 200, .offense = 105, .finisherFill = 150});
  CHECK(coopScalingFactors(rules, CoopLevel::Standard, 4, 0, false) ==
        ScalingFactors{.hp = 320, .mp = 250, .offense = 110, .finisherFill = 175});
  CHECK(coopScalingFactors(rules, CoopLevel::Standard, 4, 0, true) ==
        ScalingFactors{.hp = 370, .mp = 100, .offense = 115, .finisherFill = 175});
  // A helper adds 15 to the HP, the hard level 25 more.
  CHECK(coopScalingFactors(rules, CoopLevel::Standard, 1, 1, false).hp == 115);
  CHECK(coopScalingFactors(rules, CoopLevel::Standard, 2, 1, false).hp == 195);
  CHECK(coopScalingFactors(rules, CoopLevel::Hard, 2, 0, false).hp == 205);
  // A ruleset without the tables keeps the original numbers.
  BattleRules plain = rules;
  plain.coopScaling.reset();
  CHECK(coopScalingFactors(plain, CoopLevel::Hard, 4, 2, true) == ScalingFactors{});
}

TEST_CASE("two partners face a scaled enemy; the stat gains read its original stats") {
  BattleSetup setup = coopDuel(evenStats(400, 300), evenStats(400, 300), evenStats(1000, 200));
  setup.coopScaling = {.level = CoopLevel::Standard, .helpers = 0};
  Battle scaled = startBattle(setup, 7);
  const Fighter& enemy = scaled.state().fighters.at(1);
  CHECK(scaled.scaling() ==
        ScalingFactors{.hp = 180, .mp = 150, .offense = 100, .finisherFill = 125});
  CHECK(enemy.stats.maxHp == 1800);
  CHECK(enemy.hp == 1800);
  CHECK(enemy.stats.maxMp == 750);
  CHECK(enemy.stats.offense == 200);
  CHECK(enemy.finisherGoal == (originalRules().finisher.goalBase - 200) * 100 / 125);
  CHECK(enemy.startStats.maxHp == 1000);
  // The same battle with the option off has the original enemy.
  setup.coopScaling.level = CoopLevel::Off;
  Battle plain = startBattle(setup, 7);
  CHECK(plain.state().fighters.at(1).stats.maxHp == 1000);
  CHECK(plain.scaling() == ScalingFactors{});
}

TEST_CASE("an ally that runs off rescales the enemies, keeping their share of HP") {
  BattleSetup setup = coopDuel(evenStats(400, 100), evenStats(400, 100), evenStats(1000, 100));
  setup.coopScaling = {.level = CoopLevel::Standard, .helpers = 0};
  Battle battle = startBattle(setup, 5);
  const FighterId ally = battle.state().allies.front().id;
  REQUIRE(battle.state().fighters.at(1).stats.maxHp == 1800);
  REQUIRE(battle.command(Command::Flee, ally));
  stepUntil(battle, [&] { return battle.state().fighters[ally].gone(); });
  REQUIRE_FALSE(battle.finished());
  const Fighter& enemy = battle.state().fighters.at(1);
  CHECK(battle.scaling() == ScalingFactors{});
  CHECK(enemy.stats.maxHp == 1000);
  CHECK(enemy.hp > 0);
  CHECK(enemy.hp <= 1000);
}

TEST_CASE("a lone partner with a helper faces a bit more HP") {
  BattleSetup setup = duel(evenStats(400, 100), evenStats(1000, 100));
  setup.coopScaling = {.level = CoopLevel::Standard, .helpers = 1};
  Battle battle = startBattle(setup, 5);
  CHECK(battle.state().fighters.at(1).stats.maxHp == 1150);
  CHECK(battle.state().fighters.at(1).stats.maxMp == 500);
}

TEST_CASE("one partner of a co-op battle can be felled on its own, never an enemy") {
  Battle battle =
      startBattle(coopDuel(evenStats(400, 100), evenStats(400, 300), evenStats(400, 100)), 9);
  const FighterId ally = battle.state().allies.front().id;
  const std::int32_t enemyHp = battle.state().fighters[1].hp;
  battle.defeatPartner(1);
  battle.defeatPartner(ally);
  stepUntil(battle, [&] { return battle.state().fighters[ally].hp <= 0; });
  REQUIRE_FALSE(battle.finished());
  CHECK(battle.state().fighters[ally].hp <= 0);
  CHECK_FALSE(battle.state().fighters[ally].gone());
  CHECK(battle.state().partner().hp > 0);
  CHECK(battle.state().fighters[1].hp > enemyHp / 2);
  stepUntil(battle, [&] { return battle.state().fighters[ally].gone(); });
  REQUIRE_FALSE(battle.finished());
  CHECK(battle.state().fighters[ally].departure == Departure::KnockedOut);
}
