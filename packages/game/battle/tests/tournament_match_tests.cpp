#include <doctest/doctest.h>

#include <cstdint>

#include "bench.hpp"
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

TEST_CASE("std_main.c STD_tickBattleResultScreen: at time up with a winner the loser falls") {
  // Seeds until a time-up with a winner and one with a draw have been seen.
  bool fell = false;
  bool drawn = false;
  for (std::uint64_t seed = 1; seed < 40 && !(fell && drawn); ++seed) {
    Battle battle = startBattle(match(evenStats(9000, 20), evenStats(9000, 20), 2), seed);
    runToEnd(battle);
    const BattleResult& result = resultOf(battle);
    REQUIRE(result.timeUp);
    const Fighter& partner = battle.state().partner();
    const Fighter& rival = battle.state().fighters.at(1);
    if (result.draw) {
      // STD_tickBattleResultScreen: a draw leaves both standing.
      CHECK(partner.shown.clip != 0x2B);
      CHECK(rival.shown.clip != 0x2B);
      drawn = true;
      continue;
    }
    const Fighter& loser = result.outcome == Outcome::Victory ? rival : partner;
    const Fighter& winner = result.outcome == Outcome::Victory ? partner : rival;
    CHECK(loser.shown.clip == 0x2B);
    CHECK(loser.shown.frame == 0);
    CHECK(winner.shown.clip != 0x2B);
    fell = true;
  }
  CHECK(fell);
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

TEST_CASE("std_main.c STD_applyPartnerStatsToFighter: in a match a low level is no sure hit and "
          "no hit lands after a faint") {
  Bench cup{match(evenStats(500, 200), evenStats(500, 200), 99)};
  BattleContext context = cup.context();
  const MoveRef punch = Bench::move("punch");
  cup.fighter(1).combatant.level = 1;
  cup.fighter(1).stats.speed = 999;
  const std::int32_t chance =
      cup.strategies.damage->hitChance(context.view(), PartnerId, 1, punch).percent;
  CHECK(chance < 100);
  faint(context, PartnerId);
  CHECK(cup.state.hitsOff);
  CHECK(cup.strategies.damage->hitChance(context.view(), 1, PartnerId, punch).percent == 0);
  // The field battle keeps both: a low level is a sure hit, and a faint turns nothing off.
  Bench field{duel(evenStats(500, 200), evenStats(500, 200))};
  BattleContext fieldContext = field.context();
  field.fighter(1).combatant.level = 1;
  CHECK(field.strategies.damage->hitChance(fieldContext.view(), PartnerId, 1, punch).percent ==
        100);
  faint(fieldContext, 1);
  CHECK_FALSE(field.state.hitsOff);
}

namespace {

// Queues the group's move for a fighter, aimed at the other one, and tries to start it.
bool startsGroupMove(Bench& bench, FighterId id) {
  BattleContext context = bench.context();
  Fighter& fighter = bench.fighter(id);
  fighter.attack.reset();
  fighter.queuedMove = Bench::move("group_blast");
  fighter.target = id == PartnerId ? FighterId{1} : PartnerId;
  startQueuedMove(context, id);
  return fighter.attack.has_value();
}

} // namespace

TEST_CASE("std_main.c STD_selectMoveTarget: in a match each side waits on its own group move") {
  Bench cup{match(evenStats(500, 200), evenStats(500, 200), 99)};
  REQUIRE(startsGroupMove(cup, PartnerId));
  CHECK(cup.state.sharedCooldown == 110);
  CHECK(cup.state.rivalSharedCooldown == 0);
  // The partner's group move is still on: the rival's waits.
  CHECK_FALSE(startsGroupMove(cup, 1));
  // Once it is over the rival's starts, the partner's own timer notwithstanding.
  cup.fighter(PartnerId).attack.reset();
  CHECK(startsGroupMove(cup, 1));
  CHECK(cup.state.rivalSharedCooldown == 110);
  // The partner waits on its own timer.
  cup.fighter(1).attack.reset();
  CHECK_FALSE(startsGroupMove(cup, PartnerId));
  // In a field battle one timer holds everyone (battle_main.c: BTL_isMoveOnCooldown).
  Bench field{duel(evenStats(500, 200), evenStats(500, 200))};
  REQUIRE(startsGroupMove(field, PartnerId));
  field.fighter(PartnerId).attack.reset();
  CHECK_FALSE(startsGroupMove(field, 1));
}

TEST_CASE("std_main.c STD_tickDigimonSenile: a disobeying match fighter shows its stance") {
  Bench cup{match(evenStats(500, 200), evenStats(500, 200), 99)};
  Bench field{duel(evenStats(500, 200), evenStats(500, 200))};
  for (Bench* bench : {&cup, &field}) {
    bench->fighter(PartnerId).pose = Pose::Disobeying;
    bench->fighter(PartnerId).shown.clip = 0x24;
    updateShownClips(bench->context());
  }
  CHECK(cup.fighter(PartnerId).shown.clip == StanceClip);
  CHECK(field.fighter(PartnerId).shown.clip == DisobeyClip);
}

TEST_CASE("std_main.c STD_updateFighterStatusVisuals: a match fighter's confusion ends facing "
          "the other one") {
  Bench cup{match(evenStats(500, 200), evenStats(500, 200), 99)};
  BattleContext context = cup.context();
  Fighter& rival = cup.fighter(1);
  rival.status.confused = true;
  rival.status.confusionFrames = 1;
  rival.target = 1;
  tickStatusEffects(context);
  CHECK_FALSE(rival.status.confused);
  CHECK(rival.target == PartnerId);
}
