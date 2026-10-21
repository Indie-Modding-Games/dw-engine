#include <dwe/game/battle/strategies.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <ostream>
#include <string>

#include "fixture.hpp"

using namespace battle_test;

TEST_CASE("the original battle section reads with the decomp's numbers") {
    const BattleRules& rules = originalRules();
    CHECK(rules.limits.maxEnemies == 3);
    CHECK(rules.limits.pendingDamageCap == 9999);
    CHECK(rules.affinity.factors.at(0).at(3) == 20);
    CHECK(rules.moves.counter == "counter");
    CHECK(rules.moves.sharedCooldownFrames == 110);
    CHECK(rules.speedReserve.cap == 100);
    CHECK(rules.finisher.goalBase == 3000);
    CHECK(rules.ending.fleeFrames == 41);
    CHECK(rules.rewards.enemyFactors.size() == 3);
    CHECK(rules.menus.size() == 6);
    CHECK(rules.quirks.on(quirk::SicknessAlwaysHits));
    CHECK(rules.quirks.on(quirk::InjuryCountsAnyHit));
    CHECK(checkBattleSection(sectionOf(originalSectionText())));
}

TEST_CASE("the battle section refuses bad values with the file and the place") {
    const auto negative = readBattleRules(sectionOf(originalWith("arena_radius: 2400", "arena_radius: -1")));
    CHECK(errorOf(negative).starts_with("rules/test/battle.yaml"));
    const auto percent = readBattleRules(sectionOf(originalWith("bell_roll: -50", "bell_roll: -50\n  bogus: 1")));
    CHECK_FALSE(percent);
    const auto unknown = checkBattleSection(sectionOf(originalWith("partner_ai: original", "partner_ai: nobody")));
    CHECK(errorOf(unknown).find("nobody") != std::string::npos);
}

TEST_CASE("a changed ruleset value changes the battle: the damage cap") {
    const BattleRules capped = rulesOf(originalWith("  max: 9999", "  max: 1"));
    Battle battle = startBattle(duel(evenStats(200, 100), evenStats(200, 100)), 3, capped);
    const BattleEvents events = runToEnd(battle);
    bool sawHit = false;
    for (const BattleEvent& event : events) {
        if (event.kind == EventKind::AttackHit) {
            sawHit = true;
            CHECK(event.amount == 1);
        }
    }
    CHECK(sawHit);
}

namespace {

// A test damage strategy: every attack hits for exactly 7.
class SevenDamage final : public DamageFormula {
public:
    HitChance hitChance(const BattleView& /*view*/, FighterId /*attacker*/, FighterId /*defender*/,
                        MoveRef /*move*/) override {
        return {.percent = 100, .breaksBlock = false};
    }
    std::int32_t damage(const BattleView& /*view*/, FighterId /*attacker*/, FighterId /*defender*/,
                        MoveRef /*move*/) override {
        return 7;
    }
};

} // namespace

TEST_CASE("a ruleset switches a strategy by name") {
    damageStrategies().add("seven", [] { return std::make_unique<SevenDamage>(); });
    const auto section = sectionOf(originalWith("damage: original", "damage: seven"));
    REQUIRE(checkBattleSection(section));
    auto strategies = createStrategies(section);
    REQUIRE_MESSAGE(strategies, errorOf(strategies));
    auto battle = Battle::start(duel(evenStats(100, 100), evenStats(100, 100)), originalRules(), testCatalog(),
                                std::move(*strategies), 5);
    REQUIRE(battle);
    const BattleEvents events = runToEnd(*battle);
    std::int32_t hits = 0;
    for (const BattleEvent& event : events) {
        if (event.kind == EventKind::AttackHit) {
            ++hits;
            CHECK(event.amount == 7);
        }
    }
    CHECK(hits > 0);
    CHECK_FALSE(hasEvent(events, EventKind::AttackBlocked));
}

TEST_CASE("the catalog reads the moves as the battle uses them") {
    const MoveCatalog& moves = testCatalog();
    const BattleMove& punch = moves.at(refOf(moves, "punch"));
    CHECK(punch.mpCost == 15);
    CHECK(punch.needsTarget);
    CHECK(punch.learnable);
    CHECK(moves.at(refOf(moves, "big_finish")).finisher);
    CHECK(moves.at(refOf(moves, "counter")).counter);
    CHECK(moves.at(refOf(moves, "muscle_charge")).buff.has_value());
    CHECK(moves.learnedWith(refOf(moves, "dynamite_kick")).size() == 1);
    CHECK(moves.at(moves.flattenedShot()).id == "move_121");

    std::vector<dwe::assets::MoveEntry> kept{std::from_range, testMoveTable().entries()};
    std::erase_if(kept, [](const dwe::assets::MoveEntry& entry) { return entry.id == "counter"; });
    CHECK(errorOf(MoveCatalog::build(dwe::assets::MoveTable{std::move(kept)}, originalRules())).find("counter") !=
          std::string::npos);
}

TEST_CASE("a pack's move is a finisher or learnable only when the battle section names its id") {
    // The engine numbered the pack's moves inside the original's finisher range and below the learnable limit.
    std::vector<dwe::assets::MoveEntry> entries{std::from_range, testMoveTable().entries()};
    entries.push_back(testMove("my_mod:blaze", 70, Melee, 150, 0, 0, 1));
    entries.back().originalIndex = false;
    entries.push_back(testMove("my_mod:ember", 7, Ranged, 20, 5, Near, 0));
    entries.back().originalIndex = false;
    const dwe::assets::MoveTable table{std::move(entries)};

    const auto unnamed = MoveCatalog::build(table, originalRules());
    REQUIRE_MESSAGE(unnamed.has_value(), errorOf(unnamed));
    CHECK_FALSE(unnamed->at(refOf(*unnamed, "my_mod:blaze")).finisher);
    CHECK_FALSE(unnamed->at(refOf(*unnamed, "my_mod:ember")).learnable);

    BattleRules rules = originalRules();
    rules.moves.finishers.ids = {"my_mod:blaze"};
    rules.moves.learnable.ids = {"my_mod:ember"};
    const auto named = MoveCatalog::build(table, rules);
    REQUIRE_MESSAGE(named.has_value(), errorOf(named));
    CHECK(named->at(refOf(*named, "my_mod:blaze")).finisher);
    CHECK(named->at(refOf(*named, "my_mod:ember")).learnable);

    rules.moves.finishers.ids = {"my_mod:nothing"};
    CHECK(errorOf(MoveCatalog::build(table, rules)).find("my_mod:nothing") != std::string::npos);
}
