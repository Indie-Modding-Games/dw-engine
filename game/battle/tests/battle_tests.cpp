#include <dwe/game/battle/hit_particles.hpp>
#include <dwe/game/battle/joining.hpp>
#include <dwe/game/battle/simulation.hpp>
#include <dwe/game/battle/text_keys.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "../src/aftermath.hpp"
#include "../src/usability.hpp"
#include "bench.hpp"
#include "fixture.hpp"

using namespace battle_test;

TEST_CASE("a battle needs one to three enemies and valid slots") {
  BattleSetup none = duel(evenStats(100, 100), evenStats(100, 100));
  none.enemies.clear();
  CHECK_FALSE(Battle::start(none, originalRules(), testCatalog(), originalStrategies(), 1));

  BattleSetup finisherFirst = duel(evenStats(100, 100), evenStats(100, 100), {"big_finish"});
  CHECK(
      errorOf(Battle::start(finisherFirst, originalRules(), testCatalog(), originalStrategies(), 1))
          .find("fourth slot") != std::string::npos);
  BattleSetup techniqueLast =
      duel(evenStats(100, 100), evenStats(100, 100), {"punch", "", "", "shot"});
  CHECK_FALSE(
      Battle::start(techniqueLast, originalRules(), testCatalog(), originalStrategies(), 1));
  BattleSetup unknown = duel(evenStats(100, 100), evenStats(100, 100), {"nothing"});
  CHECK_FALSE(Battle::start(unknown, originalRules(), testCatalog(), originalStrategies(), 1));
}

TEST_CASE("the fighters start with the setup's values: reserve, buff uses, finisher goal") {
  Battle battle = startBattle(duel(evenStats(100, 450), evenStats(100, 250)), 1);
  const Fighter& partner = battle.state().partner();
  CHECK(partner.speedReserve == 100);
  CHECK(partner.buffsLeft == 4);
  CHECK(partner.buffPriority == (450 / 10) + 5);
  CHECK(partner.finisherGoal == 3000 - 450);
  CHECK(battle.state().fighters.at(1).buffsLeft == (250 / 100) + 1);
  CHECK(battle.state().frame == 1);
}

TEST_CASE("a partner's move variant replaces the table's move for it alone") {
  MoveCatalog moves = testCatalog();
  const MoveRef punch = refOf(moves, "punch");
  const MoveRef variant = moves.addVariant(punch, 240, 1);
  CHECK(variant != punch);
  CHECK(moves.at(variant).id == "punch");
  CHECK(moves.at(variant).power == 240);
  CHECK(moves.at(variant).iframes == 1);
  CHECK_FALSE(moves.at(variant).learnable);
  // The table's move keeps its values and its id still finds it.
  CHECK(moves.at(punch).power == 30);
  CHECK(refOf(moves, "punch") == punch);

  BattleSetup setup = duel(evenStats(500, 100), evenStats(500, 100));
  setup.partner.moveVariants.emplace("punch", variant);
  auto battle = Battle::start(std::move(setup), originalRules(), moves, originalStrategies(), 1);
  REQUIRE_MESSAGE(battle, errorOf(battle));
  CHECK(battle->state().partner().moves.at(0) == variant);
  CHECK(battle->state().fighters.at(1).moves.at(0) == punch);
}

TEST_CASE("a sick partner loses a fifth of its offense, defense, speed and brain") {
  BattleSetup setup = duel(evenStats(100, 100), evenStats(100, 100));
  setup.condition.sick = true;
  Battle battle = startBattle(setup, 1);
  const FighterStats& stats = battle.state().partner().stats;
  CHECK(stats.offense == 80);
  CHECK(stats.defense == 80);
  CHECK(stats.speed == 80);
  CHECK(stats.brain == 80);
  CHECK(stats.maxHp == 100);
}

TEST_CASE("the menu grows with brain, and a story battle has no flee") {
  BattleSetup low = duel(evenStats(100, 50), evenStats(100, 50));
  CHECK(startBattle(low, 1).menu() ==
        std::vector{Command::Finisher, Command::Flee, Command::YourCall});
  BattleSetup smart = duel(evenStats(100, 600), evenStats(100, 50), {"punch", "shot", ""});
  CHECK(startBattle(smart, 1).menu() == std::vector{Command::Finisher, Command::Flee,
                                                    Command::Change, Command::Defend,
                                                    Command::Distance, Command::Technique2,
                                                    Command::Technique1, Command::YourCall});
  // Equipped techniques open no command below brain 500: measured in the original at brain 70,
  // three commands (finisher, flee, your call) with a technique equipped.
  BattleSetup belowTechniques =
      duel(evenStats(100, 499), evenStats(100, 50), {"punch", "shot", ""});
  const std::vector<Command> belowMenu = startBattle(belowTechniques, 1).menu();
  CHECK(std::ranges::find(belowMenu, Command::Technique1) == belowMenu.end());
  CHECK(std::ranges::find(belowMenu, Command::Technique2) == belowMenu.end());
  smart.scripted = true;
  const std::vector<Command> scripted = startBattle(smart, 1).menu();
  CHECK(std::ranges::find(scripted, Command::Flee) == scripted.end());

  const BattleRules fixed = rulesOf(originalWith("smart_menu_drops_attack_and_moderate: true",
                                                 "smart_menu_drops_attack_and_moderate: false"));
  smart.scripted = false;
  const std::vector<Command> full = startBattle(smart, 1, fixed).menu();
  CHECK(std::ranges::find(full, Command::Attack) != full.end());
  CHECK(std::ranges::find(full, Command::Moderate) != full.end());
}

TEST_CASE("commands outside the menu, in a battle without the tamer or after the end are refused") {
  Battle battle = startBattle(duel(evenStats(100, 50), evenStats(100, 50)), 1);
  CHECK_FALSE(battle.command(Command::Attack));
  CHECK(errorOf(battle.command(Command::Finisher)).find("gauge") != std::string::npos);
  CHECK(battle.command(Command::YourCall));

  BattleSetup tamerless = duel(evenStats(100, 50), evenStats(100, 50));
  tamerless.tamerless = true;
  Battle alone = startBattle(tamerless, 1);
  CHECK_FALSE(alone.command(Command::YourCall));
}

TEST_CASE("a battle plays to a victory with rewards and care changes") {
  BattleSetup setup = duel(evenStats(400, 300), evenStats(40, 20));
  Battle battle = startBattle(setup, 7);
  const BattleEvents events = runToEnd(battle);
  REQUIRE(battle.finished());
  const BattleResult& result = resultOf(battle);
  CHECK(result.outcome == Outcome::Victory);
  CHECK(result.bits == 50);
  CHECK(result.care.happiness == 2);
  CHECK(result.care.battles == 1);
  CHECK(result.care.tiredness >= 5);
  CHECK(result.foodChange == -15);
  CHECK(result.elapsedMinutes == 20);
  CHECK(result.counters.attacks > 0);
  CHECK(hasEvent(events, EventKind::Fainted));
  CHECK(hasEvent(events, EventKind::BattleEnded));
  CHECK(result.kind == BattleKind::Normal);
  CHECK_FALSE(result.recruitJoined);
}

TEST_CASE(
    "the result hands back the battle's kind and the recruit that joins the city after a victory") {
  BattleSetup setup = duel(evenStats(400, 300), evenStats(40, 20));
  setup.kind = BattleKind::Arena;
  setup.recruitOnVictory = "foe";
  Battle won = startBattle(setup, 7);
  runToEnd(won);
  REQUIRE(resultOf(won).outcome == Outcome::Victory);
  CHECK(resultOf(won).kind == BattleKind::Arena);
  CHECK(resultOf(won).recruitJoined == std::optional<std::string>{"foe"});
  Battle fled = startBattle(setup, 7);
  REQUIRE(fled.command(Command::Flee));
  runToEnd(fled);
  CHECK_FALSE(resultOf(fled).recruitJoined);
}

TEST_CASE("a partner that falls loses the battle when the countdown ends, unless revived") {
  BattleSetup setup = duel(evenStats(30, 20), evenStats(500, 400));
  Battle battle = startBattle(setup, 9);
  while (!battle.state().partner().fainted && battle.state().frame < 20000) {
    battle.step();
  }
  REQUIRE(battle.state().partner().fainted);
  CHECK(battle.revivePartner(10));
  CHECK_FALSE(battle.state().partner().fainted);
  runToEnd(battle);
  REQUIRE(battle.finished());
  CHECK(resultOf(battle).outcome == Outcome::Defeat);
  CHECK(resultOf(battle).livesLost == 1);
  CHECK(resultOf(battle).care.happiness == -30);
}

namespace {

// A battle whose enemies are beaten at once, with the partner's last HP drain on the frame before
// their fall ends it: at `hp` HP it is hit for `hp`, less `spared`.
BattleResult partnerHitAsTheEnemyFalls(std::int32_t hp, std::int32_t spared) {
  BattleSetup setup = duel(evenStats(400, 300), evenStats(40, 20));
  Battle probe = startBattle(setup, 7);
  probe.defeatEnemies();
  runToEnd(probe);
  REQUIRE(resultOf(probe).outcome == Outcome::Victory);
  const std::int64_t endFrame = probe.state().frame;
  setup.partner.hp = hp;
  Battle battle = startBattle(setup, 7);
  battle.defeatEnemies();
  while (battle.state().frame < endFrame - 1) {
    battle.step();
  }
  battle.defeatPartner();
  battle.restorePartner(spared, 0);
  runToEnd(battle);
  REQUIRE(battle.finished());
  return resultOf(battle);
}

} // namespace

// battle_main.c: BTL_handleVictorySequence ticks the battle frames before the end box, so the
// partner's HP bar drains the damage still on its way. Measured in the original: 300 pending as
// the victory starts take the partner from 727 HP to 427.
TEST_CASE("a hit landing with the last enemy's fall still costs the partner its HP") {
  const BattleResult won = partnerHitAsTheEnemyFalls(4, 1);
  CHECK(won.outcome == Outcome::Victory);
  CHECK(won.partnerHp == 1);
}

// battle_setup.c: BTL_isBattleFinished wins only while the partner's HP outlasts its damage buffer.
// Measured in the original (Agumon against Agumon, both buffers lethal on one frame): both faint,
// the countdown runs and the battle is lost; a buffer of HP - 1 wins with 1 HP left.
TEST_CASE(
    "a lethal hit landing with the last enemy's fall loses the battle, one short of it wins") {
  const BattleResult lethal = partnerHitAsTheEnemyFalls(1, 0);
  CHECK(lethal.outcome == Outcome::Defeat);
  CHECK(lethal.livesLost == 1);
  const BattleResult survives = partnerHitAsTheEnemyFalls(2, 1);
  CHECK(survives.outcome == Outcome::Victory);
  CHECK(survives.partnerHp == 1);
}

TEST_CASE("a lost battle gives the partner back the section's HP and MP, up to the maximums") {
  // dg 212, section 0x48 (the death map's script): add_stat 6 and 7; script_value.c:
  // enforceStatsLimits.
  const auto lose = [](const BattleRules& rules) {
    Battle battle = startBattle(duel(evenStats(30, 20), evenStats(500, 400)), 9, rules);
    runToEnd(battle);
    REQUIRE(resultOf(battle).outcome == Outcome::Defeat);
    return resultOf(battle);
  };
  const BattleResult full = lose(originalRules());
  CHECK(full.partnerHp == 30);
  CHECK(full.partnerMp == 500);
  const BattleResult partly = lose(rulesOf(
      originalWith("defeat_restore: { hp: 9999, mp: 9999 }", "defeat_restore: { hp: 7, mp: 0 }")));
  CHECK(partly.partnerHp == 7);
}

TEST_CASE("fleeing ends the battle after the flee frames") {
  Battle battle = startBattle(duel(evenStats(300, 100), evenStats(300, 100)), 2);
  REQUIRE(battle.command(Command::Flee));
  std::int64_t steps = 0;
  while (!battle.finished()) {
    battle.step();
    ++steps;
  }
  CHECK(resultOf(battle).outcome == Outcome::Fled);
  CHECK(steps == 42);
  CHECK(resultOf(battle).care.happiness == -10);
}

TEST_CASE("a partner called back takes no other command, the finisher included, and wins no bits") {
  // battle_tick.c: handleCommands returns before reading the buttons while currentCommand is 1.
  BattleSetup setup =
      duel(evenStats(2000, 300), evenStats(2000, 100), {"punch", "", "", "big_finish"});
  Battle battle = startBattle(setup, 11);
  while (battle.state().partner().finisherProgress < battle.state().partner().finisherGoal &&
         battle.state().frame < 5000) {
    battle.step();
  }
  REQUIRE(battle.state().partner().finisherProgress == battle.state().partner().finisherGoal);
  REQUIRE(battle.command(Command::Flee));
  battle.step();
  CHECK_FALSE(battle.command(Command::Finisher));
  CHECK_FALSE(battle.command(Command::YourCall));
  CHECK_FALSE(battle.command(Command::Flee));
  CHECK_FALSE(battle.openBag());
  std::int64_t steps = 1;
  while (!battle.finished() && steps < 400) {
    CHECK(battle.state().commands.current == Command::Flee);
    CHECK_FALSE(battle.state().frozenByFinisher());
    battle.step();
    ++steps;
  }
  REQUIRE(battle.finished());
  // The flee frames still end it on time, with nothing gained: BTL_deinitializeCombat skips
  // BTL_handleVictorySequence (no bits, no drops, no stats) when the command is 1.
  CHECK(steps == 42);
  const BattleResult& fled = resultOf(battle);
  CHECK(fled.outcome == Outcome::Fled);
  CHECK(fled.bits == 0);
  CHECK(fled.drops.empty());
  CHECK(std::ranges::all_of(fled.statGains, [](std::int32_t gain) { return gain == 0; }));
}

TEST_CASE(
    "fleeing costs happiness and discipline and tires more; a battle that ends without MP tires 10 more") {
  // tamer.c: startBattle (a flight); partner_impl.c: handlePostBattleTiredness: +5, and with the MP
  // under 80 % spent / max × 10, which the original divides first (broken_mp_tiredness): only an
  // empty MP adds anything.
  const auto fleeWithMp = [](std::int32_t mp, const BattleRules& rules) {
    BattleSetup setup = duel(evenStats(300, 100), evenStats(300, 100));
    setup.partner.mp = mp;
    Battle battle = startBattle(setup, 2, rules);
    REQUIRE(battle.command(Command::Flee));
    runToEnd(battle);
    REQUIRE(resultOf(battle).outcome == Outcome::Fled);
    return resultOf(battle);
  };
  const BattleResult fled = fleeWithMp(500, originalRules());
  CHECK(fled.care.happiness == -10);
  CHECK(fled.care.discipline == -6);
  CHECK(fled.care.tiredness == 2 + 5);
  CHECK(fled.foodChange == -15);
  CHECK(fleeWithMp(0, originalRules()).care.tiredness == 2 + 5 + 10);
  CHECK(fleeWithMp(250, originalRules()).care.tiredness == 2 + 5);
  const BattleRules exact =
      rulesOf(originalWith("broken_mp_tiredness: true", "broken_mp_tiredness: false"));
  CHECK(fleeWithMp(250, exact).care.tiredness == 2 + 5 + 5);
}

TEST_CASE("the same seed replays the same battle, event by event") {
  const BattleSetup setup = duel(evenStats(300, 150), evenStats(300, 150),
                                 {"punch", "shot", "poison_bite"}, {"punch", "shot", "dizzy_ray"});
  Battle first = startBattle(setup, 1234);
  Battle second = startBattle(setup, 1234);
  const BattleEvents a = runToEnd(first);
  const BattleEvents b = runToEnd(second);
  REQUIRE(a.size() == b.size());
  for (std::size_t index = 0; index < a.size(); ++index) {
    CHECK(a[index].kind == b[index].kind);
    CHECK(a[index].fighter == b[index].fighter);
    CHECK(a[index].amount == b[index].amount);
  }
  CHECK(resultOf(first).frames == resultOf(second).frames);
  CHECK(resultOf(first).partnerHp == resultOf(second).partnerHp);
}

TEST_CASE("with the original's rolls the first decisions are the original's: the partner waits") {
  // The Native Forest fight in the original: the generator's state as the fight began was
  // 0x5CF299CB; the partner's first roll (63 of 65) fell on an empty slot, so it waited 0x50 frames
  // (battle_main.c: BTL_selectPartnerMove) and first attacked 4 s in, while the enemy chose its
  // move on the first frame.
  const BattleSetup setup = duel(evenStats(800, 70), evenStats(400, 30), {"shot"}, {"punch"});
  Battle original = startBattle(setup, 1);
  original.useOriginalRolls(0x5CF299CBU);
  const BattleEvents& first = original.step();
  const Fighter& partner = original.state().partner();
  CHECK(partner.chargingUp);
  CHECK(partner.cooldown == originalRules().pacing.noMoveFrames);
  CHECK_FALSE(partner.queuedMove.has_value());
  CHECK(std::ranges::any_of(first, [](const BattleEvent& event) {
    return event.kind == EventKind::MoveQueued && event.fighter == 1;
  }));
  // The wait ends on frame 80, as in the original (it chose again on frame 81).
  std::int64_t waited = 1;
  while (original.state().partner().chargingUp && original.state().frame < 200) {
    waited = original.state().frame;
    original.step();
  }
  CHECK(waited == originalRules().pacing.noMoveFrames);
}

TEST_CASE("advance runs the frames real time holds at 20 a second") {
  Battle battle = startBattle(duel(evenStats(300, 100), evenStats(300, 100)), 4);
  battle.advance(0.2);
  CHECK(battle.state().frame == 5);
  battle.advance(0.025);
  battle.advance(0.025);
  CHECK(battle.state().frame == 6);
  // A long frame (a breakpoint) runs as a quarter of a second at most.
  battle.advance(10.0);
  CHECK(battle.state().frame == 11);
}

TEST_CASE("the speed reserve grows every other frame by speed/100+1 up to 100") {
  Bench bench{duel(evenStats(100, 250), evenStats(100, 100))};
  bench.fighter(PartnerId).speedReserve = 10;
  bench.state.frame = 2;
  BattleContext context = bench.context();
  tickPartnerAi(context);
  CHECK(bench.fighter(PartnerId).speedReserve == 13);
}

TEST_CASE("the MP check and the payment round in different orders") {
  const BattleRules& rules = originalRules();
  CHECK(partnerMpCost(rules, 15, 999, true, MpCostUse::Check) == 18);
  CHECK(partnerMpCost(rules, 15, 999, true, MpCostUse::Payment) == 17);
  CHECK(partnerMpCost(rules, 15, 100, false, MpCostUse::Payment) == 15);
  const BattleRules fixed = rulesOf(originalWith("mp_check_and_payment_round_differently: true",
                                                 "mp_check_and_payment_round_differently: false"));
  CHECK(partnerMpCost(fixed, 15, 999, true, MpCostUse::Check) == 17);
}

TEST_CASE("a move is usable with the MP for it, a target when it needs one and buff uses left") {
  Bench bench{
      duel(evenStats(100, 100), evenStats(100, 100), {"punch", "muscle_charge", "counter"})};
  const BattleView view = bench.context().view();
  bench.fighter(PartnerId).target = 1;
  CHECK(moveUsable(view, PartnerId, 0));
  CHECK_FALSE(moveUsable(view, PartnerId, 2));
  bench.fighter(PartnerId).buffsLeft = 0;
  CHECK_FALSE(moveUsable(view, PartnerId, 1));
  bench.fighter(PartnerId).mp = 1;
  CHECK_FALSE(moveUsable(view, PartnerId, 0));
}

TEST_CASE("the damage formula: affinity, power, the stat difference and a 90 to 110 percent roll") {
  Bench bench{duel(evenStats(500, 200), evenStats(500, 200))};
  const MoveRef punch = Bench::move("punch");
  BattleContext context = bench.context();
  // Same offense and defense: the affinities of the three slots add up to 30 (10 for special 1
  // against special 1, 10 for each empty slot), * power 30 / 30 = 30, then the roll.
  for (int roll = 0; roll < 50; ++roll) {
    const std::int32_t damage =
        bench.strategies.damage->damage(context.view(), PartnerId, 1, punch);
    CHECK(damage >= 27);
    CHECK(damage <= 33);
  }
}

TEST_CASE("the hit chance: low levels always hit, a block stops all but a finisher") {
  Bench bench{duel(evenStats(500, 200), evenStats(500, 200))};
  BattleContext context = bench.context();
  const MoveRef punch = Bench::move("punch");
  bench.fighter(1).combatant.level = 1;
  CHECK(bench.strategies.damage->hitChance(context.view(), PartnerId, 1, punch).percent == 100);
  bench.fighter(1).combatant.level = 4;
  bench.fighter(1).blocking = true;
  CHECK(bench.strategies.damage->hitChance(context.view(), PartnerId, 1, punch).percent == 0);
  const HitChance finisher =
      bench.strategies.damage->hitChance(context.view(), PartnerId, 1, Bench::move("big_finish"));
  CHECK(finisher.percent == 100);
  CHECK(finisher.breaksBlock);
  bench.fighter(1).blocking = false;
  bench.fighter(1).stats.speed = 999;
  CHECK(bench.strategies.damage->hitChance(context.view(), PartnerId, 1, punch).percent < 100);
}

TEST_CASE("pending damage drains in the original's steps") {
  Bench bench{duel(evenStats(5000, 100), evenStats(500, 100))};
  bench.fighter(PartnerId).pendingDamage = 1100;
  BattleContext context = bench.context();
  drainPendingDamage(context);
  CHECK(bench.fighter(PartnerId).pendingDamage == 1100 - 900 - 80 - 6 - 1);
  CHECK(bench.fighter(PartnerId).hp == 5000 - 987);
}

TEST_CASE("poison takes 1 to 3 percent of the maximum HP every 100 frames") {
  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100))};
  BattleContext context = bench.context();
  applyMoveStatus(context, PartnerId, Bench::move("poison_bite"));
  REQUIRE(bench.fighter(PartnerId).status.poisoned);
  CHECK(bench.state.counters.statusesTaken == 1);
  for (int frame = 0; frame < 100; ++frame) {
    tickStatusEffects(context);
  }
  CHECK(bench.fighter(PartnerId).pendingDamage >= 10);
  CHECK(bench.fighter(PartnerId).pendingDamage <= 30);
  CHECK(hasEvent(bench.events, EventKind::PoisonDamage));
}

// battle_main.c: BTL_tickStatusEffects: each poison tick rolls randomLimit(3) + 1 percent of the
// maximum HP, adds it to the damage on its way, shows it over the fighter in the poison's colour
// (addEntityText(entity, i, 0xc, dmg, 0)) and, for an enemy, shows its HP bar again. Measured in
// the original (2026-11-04): 12 over a 400 HP enemy, 24 over the 800 HP partner, no sound.
TEST_CASE("a poison tick shows the HP it takes over the fighter and brings back the enemy's bar") {
  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100))};
  BattleContext context = bench.context();
  Fighter& enemy = bench.fighter(1);
  enemy.status.poisoned = true;
  enemy.status.poisonFrames = 1;
  enemy.hpBarFrames = context.rules->hud.enemyHpBarFrames;
  dwe::ruleset::Random expected = bench.random;
  const std::int32_t damage = 500 * (expected.below(3) + 1) / 100;
  tickStatusEffects(context);
  CHECK(bench.random.state() == expected.state());
  CHECK(enemy.pendingDamage == damage);
  CHECK(enemy.status.poisonFrames == context.rules->status.poisonEveryFrames);
  const auto ticks = std::ranges::count_if(bench.events, [&](const BattleEvent& event) {
    return event.kind == EventKind::PoisonDamage && event.fighter == 1 && event.amount == damage;
  });
  CHECK(ticks == 1);
  tickHpBars(context);
  CHECK(enemy.hpBarFrames < context.rules->hud.enemyHpBarFrames);
}

TEST_CASE("stun and confusion last their rolled frames, and a protected fighter gets none") {
  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100))};
  BattleContext context = bench.context();
  applyMoveStatus(context, 1, Bench::move("stun_slap"));
  const std::int32_t stun = bench.fighter(1).status.stunFrames;
  CHECK(stun >= 200);
  CHECK(stun <= 240);
  for (int frame = 0; frame < stun; ++frame) {
    tickStatusEffects(context);
  }
  CHECK_FALSE(bench.fighter(1).status.stunned);

  applyMoveStatus(context, 1, Bench::move("dizzy_ray"));
  CHECK(bench.fighter(1).status.confusionFrames >= 200);
  CHECK(bench.fighter(1).status.confusionFrames <= 300);
  cureStatus(context, 1);
  CHECK_FALSE(bench.fighter(1).status.confused);

  bench.fighter(1).status.protectedFromStatus = true;
  applyMoveStatus(context, 1, Bench::move("poison_bite"));
  CHECK_FALSE(bench.fighter(1).status.poisoned);
}

TEST_CASE("flattening squashes, flattens for 224 to 304 frames and squashes back") {
  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100))};
  BattleContext context = bench.context();
  applyMoveStatus(context, 1, Bench::move("squash"));
  Fighter& enemy = bench.fighter(1);
  CHECK(enemy.status.flatFrames == -1);
  enemy.status.flatFrames = 65;
  for (int frame = 0; frame < 65; ++frame) {
    tickStatusEffects(context);
  }
  REQUIRE(enemy.status.flattened);
  CHECK(enemy.status.flatFrames >= 224);
  CHECK(enemy.status.flatFrames <= 304);
  const std::int32_t left = enemy.status.flatFrames;
  for (int frame = 0; frame < left; ++frame) {
    tickStatusEffects(context);
  }
  CHECK_FALSE(enemy.status.flattened);
  CHECK(enemy.moveRange == 0);
}

TEST_CASE("the flattening's squash scales the body and swaps it for the paper sprite") {
  // battle_main.c: BTL_applyFlattenScale: 4096ths of the body, angles in 4096ths of a turn.
  CHECK_FALSE(flattenScale(0x40).has_value());
  CHECK_FALSE(flattenScale(-1).has_value());
  const auto first = flattenScale(0x3F); // stretch at angle 16
  REQUIRE(first.has_value());
  CHECK(first->up == doctest::Approx(1.0F + std::sin(16.0F * 2.0F * 3.14159265F / 4096.0F)));
  const auto squashed = flattenScale(0x2F); // squash at angle 16
  REQUIRE(squashed.has_value());
  CHECK(squashed->across > 1.0F);
  CHECK(squashed->up < 1.0F);
  CHECK(flattenScale(5)->up == doctest::Approx(0.5F));
  CHECK(flattenScale(4)->up == doctest::Approx(0.0F));
  CHECK(flattenScale(3)->across == doctest::Approx(0.25F));
  CHECK(flattenScale(0)->up == doctest::Approx(1.0F));

  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100))};
  BattleContext context = bench.context();
  applyMoveStatus(context, 1, Bench::move("squash"));
  Fighter& enemy = bench.fighter(1);
  enemy.status.flatFrames = 65;
  bool sawSprite = false;
  for (int frame = 0; frame < 65; ++frame) {
    tickStatusEffects(context);
    updateShownClips(context);
    if (enemy.status.flatFrames == 4) {
      CHECK(enemy.shown.flat.up == doctest::Approx(0.0F));
      CHECK_FALSE(enemy.shown.flat.sprite.has_value());
    }
    if (enemy.status.flatFrames == 3) {
      sawSprite = enemy.shown.flat.sprite.has_value();
    }
  }
  CHECK(sawSprite);
  REQUIRE(enemy.status.flattened);
  // renderFlatDigimon: the walking frames take turns every tenth frame.
  const std::optional<FlatSprite> walking = enemy.shown.flat.sprite;
  REQUIRE(walking.has_value());
  CHECK((*walking == FlatSprite::Walk || *walking == FlatSprite::WalkAlt));
  int turns = 0;
  for (int frame = 0; frame < 20; ++frame) {
    const std::optional<FlatSprite> before = enemy.shown.flat.sprite;
    tickStatusEffects(context);
    updateShownClips(context);
    turns += before != enemy.shown.flat.sprite ? 1 : 0;
  }
  CHECK(turns == 2);
  const std::int32_t left = enemy.status.flatFrames;
  for (int frame = 0; frame < left; ++frame) {
    tickStatusEffects(context);
    updateShownClips(context);
  }
  CHECK_FALSE(enemy.shown.flat.sprite.has_value());
  CHECK(enemy.shown.flat.up == doctest::Approx(1.0F));
}

TEST_CASE("support moves raise the caster's stats by their percentages") {
  BattleSetup setup = duel(evenStats(1000, 200), evenStats(1000, 100), {"muscle_charge"});
  Battle battle = startBattle(setup, 3);
  const BattleEvents events = runToEnd(battle, 400);
  CHECK(hasEvent(events, EventKind::Buffed));
  CHECK(battle.state().partner().stats.offense >= 260);
}

TEST_CASE("a support move shows each stat it raises, by the raise before the cap") {
  CombatStats partner = evenStats(1000, 200);
  partner.offense = 990;
  partner.defense = 105;
  partner.speed = 210;
  Battle battle = startBattle(duel(partner, evenStats(1000, 100), {"war_cry"}), 3);
  std::vector<std::pair<EventKind, std::int32_t>> shown;
  for (int frame = 0; frame < 400 && shown.empty() && !battle.finished(); ++frame) {
    for (const BattleEvent& event : battle.step()) {
      if (event.fighter == PartnerId &&
          (event.kind == EventKind::OffenseRaised || event.kind == EventKind::DefenseRaised ||
           event.kind == EventKind::SpeedRaised)) {
        shown.emplace_back(event.kind, event.amount);
      }
    }
  }
  // battle_main.c: BTL_applyBuffMove, case 0x2a: offense / 10, defense * 5 / 100, speed / 10, in
  // that order; the offense stops at 999 but shows 99.
  CHECK(shown == std::vector<std::pair<EventKind, std::int32_t>>{{EventKind::OffenseRaised, 99},
                                                                 {EventKind::DefenseRaised, 5},
                                                                 {EventKind::SpeedRaised, 21}});
  CHECK(battle.state().partner().stats.offense == 999);
}

TEST_CASE("the finisher gauge fills, the command starts the aura and presses charge it") {
  BattleSetup setup =
      duel(evenStats(2000, 300), evenStats(2000, 100), {"punch", "", "", "big_finish"});
  Battle battle = startBattle(setup, 11);
  while (battle.state().partner().finisherProgress < battle.state().partner().finisherGoal &&
         battle.state().frame < 5000) {
    battle.step();
  }
  REQUIRE(battle.state().partner().finisherProgress == battle.state().partner().finisherGoal);
  REQUIRE(battle.command(Command::Finisher));
  bool started = false;
  for (int frame = 0; frame < 400 && !started; ++frame) {
    started = hasEvent(battle.step(), EventKind::FinisherStarted);
  }
  REQUIRE(started);
  CHECK(battle.state().frozenByFinisher());
  for (int press = 0; press < 10; ++press) {
    battle.pressFinisherCharge();
    battle.step();
  }
  CHECK(battle.state().finisher.charge == 20);
}

TEST_CASE("a counter needs 165 MP under the quirk") {
  BattleSetup setup = duel(evenStats(2000, 100), evenStats(2000, 100), {"punch"}, {"counter"});
  setup.enemies.front().stats.speed = 999;
  setup.enemies.front().level = 5;
  setup.enemies.front().mp = 100;
  Battle poor = startBattle(setup, 5);
  CHECK_FALSE(hasEvent(runToEnd(poor, 4000), EventKind::Countered));
  setup.enemies.front().mp = 500;
  Battle rich = startBattle(setup, 5);
  const BattleEvents events = runToEnd(rich, 4000);
  CHECK(hasEvent(events, EventKind::Countered));
}

TEST_CASE("the change command walks through the enemies") {
  BattleSetup setup = duel(evenStats(2000, 400), evenStats(2000, 100));
  setup.enemies.push_back(fighterWith("foe2", evenStats(2000, 100), {"punch"}, {.x = 600, .z = 0}));
  setup.enemies.push_back(
      fighterWith("foe3", evenStats(2000, 100), {"punch"}, {.x = -600, .z = 0}));
  Battle battle = startBattle(setup, 8);
  std::vector<std::optional<FighterId>> targets;
  for (int round = 0; round < 3; ++round) {
    REQUIRE(battle.command(Command::Change));
    battle.step();
    battle.step();
    targets.push_back(battle.state().partner().target);
  }
  CHECK(targets.at(0) != targets.at(1));
  CHECK(targets.at(1) != targets.at(2));
}

TEST_CASE("after a victory the partner learns a seen technique of its specials, with its pair") {
  BattleSetup setup = duel(evenStats(3000, 400), evenStats(200, 100), {"punch"}, {"dynamite_kick"});
  setup.partner.speciesMoves.at(0) = "dynamite_kick";
  Battle battle = startBattle(setup, 12);
  runToEnd(battle);
  REQUIRE(resultOf(battle).outcome == Outcome::Victory);
  CHECK(resultOf(battle).learnedMoves ==
        std::vector<std::string>{"dynamite_kick", "dynamite_kick_48"});
}

TEST_CASE("the stat gains follow the strongest enemy") {
  BattleSetup setup = duel(evenStats(400, 100), evenStats(40, 400));
  setup.enemies.front().hp = 1;
  Battle battle = startBattle(setup, 13);
  runToEnd(battle);
  REQUIRE(resultOf(battle).outcome == Outcome::Victory);
  // Offense 400 against 100, one enemy (factor 10): (1000 + 4000 - 1) / 1000 = 4.
  CHECK(resultOf(battle).statGains.at(static_cast<std::size_t>(StatIndex::Offense)) == 4);
}

TEST_CASE("a beaten partner on a map with no drops gets none") {
  BattleSetup setup = duel(evenStats(400, 300), evenStats(40, 20));
  setup.enemies.front().dropItem = "meat";
  setup.enemies.front().dropChance = 100;
  Battle dropping = startBattle(setup, 6);
  runToEnd(dropping);
  const std::vector<Drop> dropped = resultOf(dropping).drops;
  REQUIRE(dropped.size() == 1);
  CHECK(dropped.front().item == "meat");
  CHECK(dropped.front().species == setup.enemies.front().species);
  CHECK(dropped.front().position == dropping.state().fighters[1].position);
  setup.map = "ogre11";
  Battle none = startBattle(setup, 6);
  runToEnd(none);
  CHECK(resultOf(none).drops.empty());
}

TEST_CASE("the injury roll comes after any hit under the quirk") {
  BattleSetup setup = duel(evenStats(400, 300), evenStats(40, 20));
  setup.condition.tiredness = 300;
  Battle battle = startBattle(setup, 6);
  runToEnd(battle);
  REQUIRE(resultOf(battle).outcome == Outcome::Victory);
  CHECK(resultOf(battle).injured == resultOf(battle).counters.anyoneHit);
}

TEST_CASE("enemies on screen join by the type table, the bell and the story") {
  const std::vector<DigimonType> candidates{DigimonType::Virus, DigimonType::Virus,
                                            DigimonType::Virus};
  dwe::ruleset::Random random{1};
  const std::vector<std::string> bell{"enemy_bell"};
  const JoinContext belled{.partnerType = DigimonType::Data, .inventory = bell, .scripted = false};
  const std::vector<bool> joined = rollJoiningEnemies(originalRules(), belled, candidates, random);
  CHECK(std::ranges::count(joined, true) == 2);
  const JoinContext story{.partnerType = DigimonType::Virus, .inventory = {}, .scripted = true};
  CHECK(std::ranges::count(rollJoiningEnemies(originalRules(), story, candidates, random), true) ==
        2);
}

TEST_CASE("runBattles counts the outcomes of many battles") {
  const BattleSetup setup = duel(evenStats(300, 150), evenStats(200, 120));
  const auto rates =
      runBattles(setup, originalRules(), testCatalog(), [] { return originalStrategies(); },
                 {.count = 20, .firstSeed = 100, .frameLimit = DefaultFrameLimit});
  REQUIRE_MESSAGE(rates, errorOf(rates));
  CHECK(rates->battles == 20);
  CHECK(rates->victories + rates->defeats + rates->flights + rates->unfinished == 20);
  CHECK(rates->victoryRate() > 0.5);
}

TEST_CASE("player-facing text is only keys") {
  CHECK(commandTextKey(Command::YourCall) == "battle.command.your_call");
  CHECK(outcomeTextKey(Outcome::Fled) == "battle.outcome.fled");
  CHECK(statusTextKey(StatusEffect::Flatten) == "battle.status.flatten");
}

namespace {

// A ground whose map stops every step.
class WalledGround final : public BattleGround {
public:
  [[nodiscard]] std::optional<ScreenPoint> screenPointOf(Point /*ground*/,
                                                         std::int32_t /*height*/) const override {
    return ScreenPoint{};
  }
  [[nodiscard]] bool blocksStep(Point /*from*/, Point /*to*/,
                                std::int32_t /*radius*/) const override {
    return true;
  }
};

// An open ground seen from above: a screen pixel is eight world units, x right and z down.
class OpenGround final : public BattleGround {
public:
  static constexpr std::int32_t UnitsPerPixel = 8;
  [[nodiscard]] std::optional<ScreenPoint> screenPointOf(Point ground,
                                                         std::int32_t height) const override {
    return ScreenPoint{.x = ground.x / UnitsPerPixel, .y = (ground.z - height) / UnitsPerPixel};
  }
  [[nodiscard]] bool blocksStep(Point /*from*/, Point /*to*/,
                                std::int32_t /*radius*/) const override {
    return false;
  }
};

// An open ground that draws every fighter's body at one point of the screen.
class BodyAtGround final : public BattleGround {
public:
  explicit BodyAtGround(ScreenPoint body) : body_(body) {}
  [[nodiscard]] std::optional<ScreenPoint> screenPointOf(Point /*ground*/,
                                                         std::int32_t /*height*/) const override {
    return ScreenPoint{};
  }
  [[nodiscard]] bool blocksStep(Point /*from*/, Point /*to*/,
                                std::int32_t /*radius*/) const override {
    return false;
  }
  [[nodiscard]] std::optional<ScreenPoint> fighterNodeOnScreen(std::size_t /*fighter*/,
                                                               std::uint32_t node) const override {
    CHECK(node == 1);
    return body_;
  }

private:
  ScreenPoint body_;
};

// Whether the enemy fought cautiously at any frame of the first `frames`.
bool everCautious(Battle& battle, std::int64_t frames) {
  bool cautious = false;
  while (!battle.finished() && battle.state().frame < frames) {
    battle.step();
    cautious = cautious || battle.state().fighters.at(1).chargeMode == ChargeMode::Cautious;
  }
  return cautious;
}

} // namespace

TEST_CASE("battle_main.c BTL_tickEnemyAI: an enemy whose body is off the screen's middle box is "
          "cautious") {
  // isOffScreen(out, 0xc8, 0xa0): more than 100 across or 80 down from the centre.
  for (const ScreenPoint body : {ScreenPoint{.x = 101, .y = 0}, ScreenPoint{.x = 0, .y = -81}}) {
    const BodyAtGround off{body};
    Battle battle = startBattle(duel(evenStats(3000, 300), evenStats(3000, 300)), 11);
    battle.setGround(&off);
    CHECK(everCautious(battle, 600));
  }
  const BodyAtGround edge{{.x = 100, .y = 80}};
  Battle battle = startBattle(duel(evenStats(3000, 300), evenStats(3000, 300)), 11);
  battle.setGround(&edge);
  CHECK_FALSE(everCautious(battle, 1980));
}

TEST_CASE("behind walls on every side the fighters only close in to strike, as the melee walk "
          "goes untested") {
  const WalledGround ground;
  for (const std::uint64_t seed : {3U, 8U}) {
    Battle battle = startBattle(duel(evenStats(400, 120), evenStats(400, 120)), seed);
    battle.setGround(&ground);
    std::vector<Point> before{battle.state().fighters[0].position,
                              battle.state().fighters[1].position};
    while (!battle.finished() && battle.state().frame < 4000) {
      battle.step();
      for (std::size_t id = 0; id < before.size(); ++id) {
        const Fighter& fighter = battle.state().fighters[id];
        const Point other = battle.state().fighters[1 - id].position;
        if (fighter.position != before[id]) {
          CHECK(distanceSquared(fighter.position, other) <= distanceSquared(before[id], other));
        }
        before[id] = fighter.position;
      }
    }
  }
}

TEST_CASE("no fighter walks out of the combat area, a rectangle of the screen") {
  const OpenGround ground;
  const TimingRules& timing = originalRules().timing;
  const std::int32_t halfWidth = timing.combatAreaWidth / 2 * OpenGround::UnitsPerPixel;
  const std::int32_t halfHeight = timing.combatAreaHeight / 2 * OpenGround::UnitsPerPixel;
  for (const std::uint64_t seed : {1U, 5U, 9U}) {
    BattleSetup setup = duel(evenStats(400, 120), evenStats(400, 120), {"punch"}, {"punch"});
    Battle battle = startBattle(setup, seed);
    battle.setGround(&ground);
    while (!battle.finished() && battle.state().frame < 4000) {
      battle.step();
      for (const Fighter& fighter : battle.state().fighters) {
        const std::int32_t slack = fighter.combatant.radius + timing.walkSpeed;
        CHECK(std::abs(fighter.position.x) <= halfWidth + slack);
        CHECK(fighter.position.z <= halfHeight + slack);
        CHECK(fighter.position.z >= -halfHeight - slack);
      }
    }
  }
}

TEST_CASE("a fighter hit or blocking loses its own shots still on their way") {
  std::int32_t checked = 0;
  for (const std::uint64_t seed : {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U}) {
    BattleSetup setup = duel(evenStats(400, 120), evenStats(400, 120), {"shot"}, {"shot"});
    setup.enemies.front().position = {.x = 0, .z = 650};
    Battle battle = startBattle(std::move(setup), seed);
    while (!battle.finished() && battle.state().frame < 3000) {
      const BattleEvents& frame = battle.step();
      for (const BattleEvent& event : frame) {
        if (event.kind != EventKind::AttackHit && event.kind != EventKind::AttackBlocked) {
          continue;
        }
        ++checked;
        // battle_main.c: BTL_tickAttackHits calls BTL_removeMoveEffect on the fighter it handles.
        CHECK(std::ranges::none_of(battle.state().inFlight, [&](const AttackInFlight& flying) {
          return flying.attacker == event.fighter;
        }));
      }
    }
  }
  CHECK(checked > 0);
}

namespace {

// How many draws `from` takes to reach `to` (both the original's generator).
int drawsBetween(dwe::ruleset::Random from, const dwe::ruleset::Random& to) {
  constexpr int Most = 1000;
  for (int draws = 0; draws < Most; ++draws) {
    if (from.state() == to.state()) {
      return draws;
    }
    static_cast<void>(from.next());
  }
  return -1;
}

using dwe::game::battle::HitParticles;

} // namespace

TEST_CASE("the original's sparks draw on the original's frames: a hit, then 43 frames") {
  // Recorded in the original (the Native Forest fight, a breakpoint on every rand() call): on frame
  // 87 the enemy's Spit Fire (1 invulnerable frame) hits the partner from the state 0x3E8E8486:
  // the hit roll 16, the turn roll 25, the damage roll 10 of 21, then the spark's 12 draws
  // (createParticleFX). After it the spark and the particles it leaves on the partner draw on
  // these frames (tickParticleFX, addEntityParticleFX, tickEntityParticleFX), nothing else draws,
  // and the partner's next pick on frame 130 rolls 47 of 65.
  const std::map<int, int> recorded{{87, 15}, {96, 3},   {98, 6},   {99, 2},   {101, 4},
                                    {102, 6}, {104, 10}, {105, 12}, {106, 12}, {107, 4},
                                    {109, 4}, {110, 6},  {113, 2},  {114, 6},  {117, 2}};
  dwe::ruleset::Random random = dwe::ruleset::Random::original(0x3E8E8486U);
  dwe::ruleset::Random before = random;
  CHECK(random.below(100) == 16);
  CHECK(random.below(100) == 25);
  CHECK(random.below(21) == 10);
  HitParticles sparks;
  sparks.spawn(random, HitParticles::Spark::Hit, 0, {}, 0, 1 + 0x10);
  for (int frame = 87; frame < 130; ++frame) {
    sparks.tick(random);
    const auto expected = recorded.find(frame);
    CAPTURE(frame);
    CHECK(drawsBetween(before, random) == (expected == recorded.end() ? 0 : expected->second));
    before = random;
  }
  CHECK(sparks.sparks() == 0);
  CHECK(sparks.fighterParticles() == 0);
  CHECK(random.below(65) == 47);
}

TEST_CASE("the original's block spark draws 8 as it starts and nothing after") {
  // Recorded: on frame 22 the enemy's Spit Fire misses the partner from the state 0xA42DBE4C (the
  // roll 64 over a chance of 42), the block's spark draws 8, and the next draws are the enemy's on
  // frame 69: charge mode 0 of 2 and its move, 29 of 120.
  dwe::ruleset::Random random = dwe::ruleset::Random::original(0xA42DBE4CU);
  CHECK(random.below(100) == 64);
  HitParticles sparks;
  dwe::ruleset::Random before = random;
  sparks.spawn(random, HitParticles::Spark::Block, 0, {}, 0, 0x11);
  CHECK(drawsBetween(before, random) == 8);
  before = random;
  for (int frame = 22; frame < 69; ++frame) {
    sparks.tick(random);
  }
  CHECK(drawsBetween(before, random) == 0);
  CHECK(sparks.sparks() == 0);
  CHECK(random.below(2) == 0);
  CHECK(random.below(120) == 29);
}

TEST_CASE("only 4 sparks roll at once; a spark the original never shows rolls and keeps no slot") {
  dwe::ruleset::Random random = dwe::ruleset::Random::original(1);
  HitParticles sparks;
  for (std::size_t spark = 0; spark < HitParticles::SparkSlots; ++spark) {
    sparks.spawn(random, HitParticles::Spark::Block, 0, {}, 0, 0x11);
  }
  dwe::ruleset::Random before = random;
  sparks.spawn(random, HitParticles::Spark::Hit, 0, {}, 0, 0x11);
  CHECK(drawsBetween(before, random) == 0);
  CHECK(sparks.sparks() == HitParticles::SparkSlots);
  HitParticles unseen;
  before = random;
  unseen.spawn(random, HitParticles::Spark::Hit, HitParticles::SparkKinds, {}, 0, 0x11);
  CHECK(drawsBetween(before, random) == 12);
  CHECK(unseen.sparks() == 0);
}

TEST_CASE("an effect's attacker particle draws its node, ticks on its own frame, then every 4th") {
  // efe.c: addEntityParticleFX draws the node (1), and its object comes after the effects'
  // engine, so tickEntityParticleFX runs it on the same frame: 12 % 4 == 0 draws 2. Then 2 more
  // when its timer reaches 8 and 4, and it ends at 0 without drawing.
  dwe::ruleset::Random random = dwe::ruleset::Random::original(1);
  HitParticles particles;
  dwe::ruleset::Random before = random;
  particles.attach(random, 0, 12);
  CHECK(drawsBetween(before, random) == 3);
  CHECK(particles.fighterParticles() == 1);
  for (int frame = 1; frame <= 12; ++frame) {
    before = random;
    particles.tick(random);
    CAPTURE(frame);
    CHECK(drawsBetween(before, random) == (frame == 4 || frame == 8 ? 2 : 0));
  }
  CHECK(particles.fighterParticles() == 0);
}

TEST_CASE("an effect's attacker particle draws nothing while the 20 fighter particles are taken") {
  dwe::ruleset::Random random = dwe::ruleset::Random::original(1);
  HitParticles particles;
  for (std::size_t particle = 0; particle < HitParticles::FighterParticleSlots; ++particle) {
    particles.attach(random, 0, 0x30);
  }
  CHECK(particles.fighterParticles() == HitParticles::FighterParticleSlots);
  const dwe::ruleset::Random before = random;
  particles.attach(random, 0, 12);
  CHECK(drawsBetween(before, random) == 0);
}

TEST_CASE("a hit's spark keeps the original's spreads, frames and fighter particle nodes") {
  // Measured in the original (the Native Forest fight, vblank 28040): BTL_startHitAnimation calls
  // createParticleFX(kind 1, count 1, (621, -85, 3464), the partner, 0x11) from the state
  // 0x9AAB7223, which leaves PARTICLE_FX_DATA[0] with timer 22, endFrame 21 and these spreads.
  dwe::ruleset::Random random = dwe::ruleset::Random::original(0x9AAB7223U);
  HitParticles sparks;
  sparks.spawn(random, HitParticles::Spark::Hit, 1, {621, -85, 3464}, 0, 0x11);
  REQUIRE(sparks.sparkSlots()[0].has_value());
  const HitParticles::SparkFx& spark = *sparks.sparkSlots()[0];
  CHECK(spark.state == 0);
  CHECK(spark.timer == 22);
  CHECK(spark.endFrame == 21);
  CHECK(spark.position == HitParticles::Vec3{621, -85, 3464});
  using Row = std::array<std::int32_t, 3>;
  CHECK(spark.spread[0] == Row{-44, -35, 81});
  CHECK(spark.spread[1] == Row{80, -53, -45});
  CHECK(spark.spread[2] == Row{8, 34, 41});
  CHECK(spark.spread[3] == Row{403, 405, 393});
  // Its first fighter particle (vblank 28067) rolls from the state 0x3EDD2D77 on the partner's
  // 22 nodes: ENTITY_PARTICLE_FX_DATA[0] sits on node 11 with timer 22.
  dwe::ruleset::Random node = dwe::ruleset::Random::original(0x3EDD2D77U);
  HitParticles particles;
  particles.setNodeCounts({22});
  particles.attach(node, 0, 22);
  REQUIRE(particles.fighterParticleSlots()[0].has_value());
  CHECK(particles.fighterParticleSlots()[0]->node == 11);
}

TEST_CASE("a hit's spark puffs its clouds around the fighter's feet over its last 4 frames") {
  // tickParticleFX: endFrame 3, 2, 1 and 0 leave endFrame + 2 clouds each (5 + 4 + 3 + 2), each at
  // the fighter's radius plus up to half of it from where it stands; they last 14 frames.
  dwe::ruleset::Random random = dwe::ruleset::Random::original(1);
  std::vector<dwe::game::battle::Fighter> fighters(1);
  fighters[0].position = {.x = 500, .z = -3000};
  fighters[0].combatant.radius = 100;
  HitParticles sparks;
  sparks.spawn(random, HitParticles::Spark::Hit, 0, {}, 0, 0x11);
  std::size_t most = 0;
  for (int frame = 0; frame < 60; ++frame) {
    sparks.tick(random, fighters);
    most = std::max(most, sparks.clouds().size());
    for (const HitParticles::Cloud& cloud : sparks.clouds()) {
      const double dx = cloud.x - 500;
      const double dz = cloud.z - 3000;
      const double distance = std::sqrt((dx * dx) + (dz * dz));
      CHECK(distance >= 98.0);
      CHECK(distance <= 152.0);
    }
  }
  CHECK(most == 14);
  CHECK(sparks.empty());
}

TEST_CASE("with the original's rolls a hit draws its spark after the damage roll, a block before") {
  // battle_main.c: BTL_tickAttackHits: a hit rolls its damage, then BTL_handleHitReaction starts
  // the spark (12 draws); a block starts its spark (8 draws) before the blocked melee damage roll.
  const auto attackOnEnemy = [](bool blocking, bool sparks) {
    Bench bench{duel(evenStats(500, 200), evenStats(500, 200))};
    bench.random = dwe::ruleset::Random::original(0x3E8E8486U);
    bench.fighter(1).combatant.level = blocking ? 4 : 1;
    bench.fighter(1).blocking = blocking;
    bench.fighter(1).incoming.push_back({.attacker = PartnerId,
                                         .defender = 1,
                                         .move = Bench::move("punch"),
                                         .origin = bench.fighter(PartnerId).position,
                                         .framesLeft = 0,
                                         .age = std::nullopt,
                                         .clip = std::nullopt,
                                         .hitPoint = std::nullopt,
                                         .touch = std::nullopt,
                                         .facingTarget = {}});
    BattleContext context = sparks ? bench.originalContext() : bench.context();
    tickAttackHits(context);
    return std::pair{bench.random, bench.fighter(1).pendingDamage};
  };
  const dwe::ruleset::Random start = dwe::ruleset::Random::original(0x3E8E8486U);
  const auto [hitWith, hitDamageWith] = attackOnEnemy(false, true);
  const auto [hitWithout, hitDamageWithout] = attackOnEnemy(false, false);
  CHECK(hitDamageWith > 0);
  CHECK(hitDamageWith == hitDamageWithout);
  CHECK(drawsBetween(start, hitWith) == drawsBetween(start, hitWithout) + 12);
  const auto [blockWith, blockDamageWith] = attackOnEnemy(true, true);
  const auto [blockWithout, blockDamageWithout] = attackOnEnemy(true, false);
  CHECK(blockDamageWith > 0);
  CHECK(drawsBetween(start, blockWith) == drawsBetween(start, blockWithout) + 8);
}

TEST_CASE("without the original's rolls a battle's sparks roll their own") {
  const BattleSetup setup = duel(evenStats(300, 150), evenStats(300, 150), {"punch"}, {"punch"});
  Battle plain = startBattle(setup, 99);
  CHECK(plain.originalRolls() == nullptr);
  Battle original = startBattle(setup, 99);
  original.useOriginalRolls(0x5CF299CBU);
  CHECK(original.originalRolls() != nullptr);
}

TEST_CASE("a fighter's step test lets its held root step go again, as entityCheckCollision does") {
  Bench bench{duel(evenStats(500, 200), evenStats(500, 200))};
  BattleContext context = bench.context();
  // map.c: entityCheckCollision clears animFlag bit 1 before it looks: a fighter whose last step
  // was held walks on the frame it finds its way free again.
  bench.fighter(PartnerId).rootHeld = true;
  walkToward(context, PartnerId, bench.fighter(1).position);
  CHECK_FALSE(bench.fighter(PartnerId).rootHeld);
  bench.fighter(1).rootHeld = true;
  backAwayFrom(context, 1, PartnerId);
  CHECK_FALSE(bench.fighter(1).rootHeld);
}

TEST_CASE("a brain raise past a menu or an MP discount teaches its commands or its bonus") {
  struct Case {
    std::int32_t brain;
    std::vector<MenuEntry> commands;
    std::int32_t discount;
  };
  // One enemy as clever as the partner raises the brain by 1 (battle_ui.c:
  // battleStatsGainsAndDrops).
  const std::array<Case, 5> cases{{
      {.brain = 99, .commands = {MenuEntry::Attack}, .discount = 0},
      {.brain = 150, .commands = {}, .discount = 0},
      {.brain = 299, .commands = {MenuEntry::Change, MenuEntry::Distance}, .discount = 0},
      {.brain = 499, .commands = {MenuEntry::Techniques}, .discount = 0},
      {.brain = 699, .commands = {}, .discount = 5},
  }};
  for (const Case& each : cases) {
    CAPTURE(each.brain);
    const BattleSetup setup = duel(evenStats(1000, each.brain), evenStats(1000, each.brain));
    Bench bench{setup};
    dwe::ruleset::Random random{7};
    const BattleResult result =
        settleBattle(Outcome::Victory, setup, bench.state, originalRules(), testCatalog(), random);
    REQUIRE(result.statGains.at(static_cast<std::size_t>(StatIndex::Brain)) == 1);
    CHECK(result.commandsLearned == each.commands);
    CHECK(result.mpDiscountLearned == each.discount);
    CHECK(result.partnerSpecies == "hero");
  }
}

// battle_main.c: BTL_tickFighterStates runs BTL_tickHitState before any command is read, and
// battle_tick.c: handleCommands only buffers the button: no input of the player cuts a hit short.
TEST_CASE("no command, finisher press or bag cuts the partner's hit reaction short") {
  const auto fresh = [] { return startBattle(duel(evenStats(500, 50), evenStats(500, 80)), 7); };
  Battle first = fresh();
  std::int64_t hitFrame = -1;
  while (!first.finished() && first.state().frame < 4000 && hitFrame < 0) {
    for (const BattleEvent& event : first.step()) {
      if (event.kind == EventKind::AttackHit && event.fighter == PartnerId) {
        hitFrame = first.state().frame;
      }
    }
  }
  REQUIRE(hitFrame > 0);
  enum class Press : std::uint8_t { Nothing, FinisherCharge, Bag };
  const auto reactionFrames = [&](std::optional<Command> command, Press press) {
    Battle battle = fresh();
    while (battle.state().frame < hitFrame) {
      battle.step();
    }
    std::int64_t frames = 0;
    while (battle.state().partner().knockedBack && frames < 500) {
      if (command) {
        (void)battle.command(*command);
      }
      if (press == Press::FinisherCharge) {
        battle.pressFinisherCharge();
      } else if (press == Press::Bag) {
        (void)battle.openBag();
        battle.closeBag();
      }
      battle.step();
      ++frames;
    }
    return frames;
  };
  const std::int64_t alone = reactionFrames(std::nullopt, Press::Nothing);
  CHECK(alone > 0);
  CHECK(reactionFrames(std::nullopt, Press::FinisherCharge) == alone);
  CHECK(reactionFrames(std::nullopt, Press::Bag) == alone);
  for (std::uint8_t command = 1; command <= static_cast<std::uint8_t>(Command::Finisher);
       ++command) {
    CAPTURE(command);
    CHECK(reactionFrames(static_cast<Command>(command), Press::Nothing) == alone);
  }
}
