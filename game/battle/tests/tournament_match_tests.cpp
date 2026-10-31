#include <doctest/doctest.h>

#include <cstdint>

#include "fixture.hpp"

using namespace battle_test;

namespace {

BattleSetup match(CombatStats partner, CombatStats enemy, std::int32_t seconds) {
  BattleSetup setup = duel(partner, enemy);
  setup.kind = BattleKind::Tournament;
  setup.scripted = true;
  setup.timeLimit = seconds;
  return setup;
}

} // namespace

TEST_CASE("std_main.c STD_combatMain: a won tournament match gives no rewards, care or time") {
  Battle battle = startBattle(match(evenStats(400, 300), evenStats(40, 20), 99), 7);
  CHECK_FALSE(battle.bagAvailable());
  runToEnd(battle);
  const BattleResult& result = resultOf(battle);
  CHECK(result.outcome == Outcome::Victory);
  CHECK(result.kind == BattleKind::Tournament);
  CHECK(result.bits == 0);
  CHECK(result.drops.empty());
  CHECK(result.learnedMoves.empty());
  CHECK(result.care.battles == 0);
  CHECK(result.elapsedMinutes == 0);
  CHECK_FALSE(result.timeUp);
  CHECK_FALSE(result.draw);
}

TEST_CASE("std_main.c STD_checkEndCondition: a fallen partner loses at once, with no lives lost") {
  Battle battle = startBattle(match(evenStats(30, 20), evenStats(500, 400), 99), 9);
  runToEnd(battle);
  const BattleResult& result = resultOf(battle);
  CHECK(result.outcome == Outcome::Defeat);
  CHECK(result.livesLost == 0);
  CHECK(result.partnerHp <= 0);
  // No revive countdown: the match ends as the faint has played.
  CHECK(battle.state().deathFrames == 0);
}

TEST_CASE("std_main.c STD_tickFighterCounter: a second goes every 20 counted frames") {
  Battle battle = startBattle(match(evenStats(9000, 20), evenStats(9000, 20), 5), 3);
  for (int frame = 0; frame < 18; ++frame) {
    battle.step();
  }
  CHECK(battle.state().matchClock->seconds == 5);
  battle.step();
  CHECK(battle.state().matchClock->seconds == 4);
}

TEST_CASE("std_main.c STD_checkEndCondition: at time up the side that lost less HP wins") {
  Battle battle = startBattle(match(evenStats(9000, 20), evenStats(9000, 20), 2), 5);
  runToEnd(battle);
  const BattleResult& result = resultOf(battle);
  REQUIRE(result.timeUp);
  const MatchClock& clock = *battle.state().matchClock;
  const std::int32_t partnerDamage = clock.partnerStartHp - battle.state().partner().hp;
  const std::int32_t enemyDamage = clock.enemyStartHp - battle.state().fighters.at(1).hp;
  CHECK(result.draw == (partnerDamage == enemyDamage));
  CHECK((result.outcome == Outcome::Victory) == (partnerDamage < enemyDamage));
}

namespace {

// Whether the enemy fought cautiously at any frame of the first `frames`.
bool enemyEverCautious(Battle& battle, std::int64_t frames) {
  bool cautious = false;
  while (!battle.finished() && battle.state().frame < frames) {
    battle.step();
    cautious = cautious || battle.state().fighters.at(1).chargeMode == ChargeMode::Cautious;
  }
  return cautious;
}

} // namespace

TEST_CASE(
    "std_main.c STD_tickEnemyAI: a tournament rival is sometimes cautious, a field enemy not") {
  Battle cup = startBattle(match(evenStats(3000, 300), evenStats(3000, 300), 99), 11);
  CHECK(enemyEverCautious(cup, 1980));
  Battle field = startBattle(duel(evenStats(3000, 300), evenStats(3000, 300)), 11);
  CHECK_FALSE(enemyEverCautious(field, 1980));
}
