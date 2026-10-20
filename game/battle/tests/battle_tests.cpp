#include <dwe/game/battle/joining.hpp>
#include <dwe/game/battle/simulation.hpp>
#include <dwe/game/battle/text_keys.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <ostream>
#include <string>
#include <vector>

#include "../src/usability.hpp"
#include "bench.hpp"
#include "fixture.hpp"

using namespace battle_test;

TEST_CASE("a battle needs one to three enemies and valid slots") {
    BattleSetup none = duel(evenStats(100, 100), evenStats(100, 100));
    none.enemies.clear();
    CHECK_FALSE(Battle::start(none, originalRules(), testCatalog(), originalStrategies(), 1));

    BattleSetup finisherFirst = duel(evenStats(100, 100), evenStats(100, 100), {"big_finish"});
    CHECK(errorOf(Battle::start(finisherFirst, originalRules(), testCatalog(), originalStrategies(), 1))
              .find("fourth slot") != std::string::npos);
    BattleSetup techniqueLast = duel(evenStats(100, 100), evenStats(100, 100), {"punch", "", "", "shot"});
    CHECK_FALSE(Battle::start(techniqueLast, originalRules(), testCatalog(), originalStrategies(), 1));
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
    CHECK(startBattle(low, 1).menu() == std::vector{Command::Finisher, Command::Flee, Command::YourCall});
    BattleSetup smart = duel(evenStats(100, 600), evenStats(100, 50), {"punch", "shot", ""});
    CHECK(startBattle(smart, 1).menu() == std::vector{Command::Finisher, Command::Flee, Command::Change,
                                                      Command::Defend, Command::Distance, Command::Technique2,
                                                      Command::Technique1, Command::YourCall});
    smart.scripted = true;
    const std::vector<Command> scripted = startBattle(smart, 1).menu();
    CHECK(std::ranges::find(scripted, Command::Flee) == scripted.end());

    const BattleRules fixed = rulesOf(
        originalWith("smart_menu_drops_attack_and_moderate: true", "smart_menu_drops_attack_and_moderate: false"));
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

TEST_CASE("the result hands back the battle's kind and the recruit that joins the city after a victory") {
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
    CHECK(resultOf(battle).itemLossPercent == 30);
    CHECK(resultOf(battle).care.happiness == -30);
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

TEST_CASE("the same seed replays the same battle, event by event") {
    const BattleSetup setup = duel(evenStats(300, 150), evenStats(300, 150), {"punch", "shot", "poison_bite"},
                                   {"punch", "shot", "dizzy_ray"});
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
    const BattleRules fixed = rulesOf(
        originalWith("mp_check_and_payment_round_differently: true", "mp_check_and_payment_round_differently: false"));
    CHECK(partnerMpCost(fixed, 15, 999, true, MpCostUse::Check) == 17);
}

TEST_CASE("a move is usable with the MP for it, a target when it needs one and buff uses left") {
    Bench bench{duel(evenStats(100, 100), evenStats(100, 100), {"punch", "muscle_charge", "counter"})};
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
    // Same offense and defense: the affinities of the three slots add up to 30 (10 for special 1 against special 1,
    // 10 for each empty slot), * power 30 / 30 = 30, then the roll.
    for (int roll = 0; roll < 50; ++roll) {
        const std::int32_t damage = bench.strategies.damage->damage(context.view(), PartnerId, 1, punch);
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

TEST_CASE("support moves raise the caster's stats by their percentages") {
    BattleSetup setup = duel(evenStats(1000, 200), evenStats(1000, 100), {"muscle_charge"});
    Battle battle = startBattle(setup, 3);
    const BattleEvents events = runToEnd(battle, 400);
    CHECK(hasEvent(events, EventKind::Buffed));
    CHECK(battle.state().partner().stats.offense >= 260);
}

TEST_CASE("the finisher gauge fills, the command starts the aura and presses charge it") {
    BattleSetup setup = duel(evenStats(2000, 300), evenStats(2000, 100), {"punch", "", "", "big_finish"});
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
    setup.enemies.push_back(fighterWith("foe3", evenStats(2000, 100), {"punch"}, {.x = -600, .z = 0}));
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
    CHECK(resultOf(battle).learnedMoves == std::vector<std::string>{"dynamite_kick", "dynamite_kick_48"});
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
    CHECK(resultOf(dropping).drops == std::vector<std::string>{"meat"});
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
    const std::vector<DigimonType> candidates{DigimonType::Virus, DigimonType::Virus, DigimonType::Virus};
    dwe::ruleset::Random random{1};
    const std::vector<std::string> bell{"enemy_bell"};
    const JoinContext belled{.partnerType = DigimonType::Data, .inventory = bell, .scripted = false};
    const std::vector<bool> joined = rollJoiningEnemies(originalRules(), belled, candidates, random);
    CHECK(std::ranges::count(joined, true) == 2);
    const JoinContext story{.partnerType = DigimonType::Virus, .inventory = {}, .scripted = true};
    CHECK(std::ranges::count(rollJoiningEnemies(originalRules(), story, candidates, random), true) == 2);
}

TEST_CASE("runBattles counts the outcomes of many battles") {
    const BattleSetup setup = duel(evenStats(300, 150), evenStats(200, 120));
    const auto rates = runBattles(setup, originalRules(), testCatalog(), [] { return originalStrategies(); },
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
