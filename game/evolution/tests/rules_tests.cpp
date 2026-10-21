#include <dwe/game/evolution/rules.hpp>
#include <dwe/game/evolution/strategies.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

#include "fixture.hpp"

namespace dwe::game::evolution {

namespace {

using testing::errorOf;
using testing::originalSectionText;
using testing::sectionFrom;

// The original section with one text replaced.
std::string originalWith(std::string_view from, std::string_view to) {
    std::string text = originalSectionText();
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

} // namespace

TEST_CASE("the game pack's evolution section has the original's numbers") {
    const EvolutionRules rules = testing::originalRules();
    CHECK(rules.selection == "original");
    CHECK(rules.scoring == "original");
    // partner_impl.c: tickConditions.
    CHECK(rules.checkHours.baby == 6);
    CHECK(rules.checkHours.inTraining == 24);
    CHECK(rules.checkHours.rookie == 72);
    CHECK(rules.checkHours.champion == 144);
    // evolution.c: getFreshEvolutionTarget.
    CHECK(rules.babyTargets.size() == 4);
    CHECK(rules.babyTargets.at("bota") == "koro");
    // evolution.c: calculateRequirementScore.
    CHECK(rules.requirements.passPoints == 3);
    CHECK(rules.requirements.weightMargin == 5);
    CHECK(rules.requirements.hpMpDivisor == 10);
    // evolution.c: getNumMasteredMoves.
    CHECK(rules.requirements.masteredMoves.below == 64);
    CHECK(rules.requirements.masteredMoves.ids.empty());
    // evolution.c: getRookieEvolutionTarget.
    CHECK(rules.fallback.rookieHours == 96);
    CHECK(rules.fallback.rookieTarget == "nume");
    // evolution.c: handleSpecialEvolutions.
    CHECK(rules.specials.rollLimit == 100);
    CHECK(rules.specials.wakeUp.size() == 4);
    CHECK(rules.specials.praiseScold.size() == 3);
    CHECK(rules.specials.praiseScold.at(1).scolded);
    // CURRENT_SCREEN == 1, by the map's id.
    CHECK(rules.specials.wakeUp.at(3).requirements.front().mapId == "mayo02");
    CHECK(rules.specials.praiseScoldOnTimer.level == 4);
    CHECK(rules.specials.praiseScoldOnTimer.hours == 360);
    // item.c: handleEvoItems.
    CHECK(rules.items.fixed.size() == 3);
    CHECK(rules.items.levelStep == 1);
    // evl.c: EVL_applyEvolution.
    CHECK(rules.statGains.ultimateLifetimeHours == 96);
    CHECK(rules.moves.specialtyBlocks == std::vector<std::int32_t>{0, 40, 8, 32, 16, 24, 49});
    CHECK(rules.moves.specialtyBlock(1) == dwe::assets::EntryClass{.ranges = {{.from = 40, .to = 48}}});
    CHECK_FALSE(rules.moves.specialtyBlock(7).has_value());
    CHECK(rules.moves.finishers == dwe::assets::EntryClass{.ranges = {{.from = 58, .to = 112}}});
    CHECK(rules.rebirth.babies.size() == 4);
    for (const std::string_view quirk : {EvolutionItemSticks, SelectionAverageCarriesOver, UnlistedBabyRestartsTimer,
                                         RookieStatTiePicksLast, TimerSpecialOverridesNormal}) {
        CHECK_MESSAGE(rules.quirks.on(quirk), quirk);
    }
}

TEST_CASE("the moves requirement counts only the learned moves below the section's limit, and those it names") {
    // evolution.c: getNumMasteredMoves reads two 32-bit words: moves 64 and up never count.
    std::vector<dwe::assets::MoveEntry> entries;
    for (const std::int32_t index : {0, 12, 63, 64, 90}) {
        entries.push_back({.id = std::format("move_{}", index), .index = index});
    }
    // A pack's move, which the engine numbered 2: no number of the original covers it.
    entries.push_back({.id = "my_mod:spark", .index = 2, .originalIndex = false});
    const dwe::assets::MoveTable moves{std::move(entries)};
    RequirementRules rules = testing::originalRules().requirements;
    const std::vector<std::string> learned{"move_0",  "move_12",      "move_63", "move_64",
                                           "move_90", "my_mod:spark", "unknown"};
    CHECK(countMasteredMoves(rules, learned, moves) == 3);
    rules.masteredMoves.below = 100;
    CHECK(countMasteredMoves(rules, learned, moves) == 5);
    rules.masteredMoves.ids = {"my_mod:spark"};
    CHECK(countMasteredMoves(rules, learned, moves) == 6);
}

TEST_CASE("the move classes read their ids, and a block's ids need the block") {
    std::string text{originalSectionText()};
    const std::string end = "  finisher_end: 113\n";
    REQUIRE(text.contains(end));
    text.replace(text.find(end), end.size(),
                 end + "  finisher_ids: [\"my_mod:nova\"]\n  specialty_block_ids: {2: [\"my_mod:ember\"]}\n");
    const auto rules = readEvolutionRules(sectionFrom(text));
    REQUIRE_MESSAGE(rules.has_value(), errorOf(rules));
    CHECK(rules->moves.finishers.ids == std::vector<std::string>{"my_mod:nova"});
    CHECK(rules->moves.specialtyBlock(2).value_or(dwe::assets::EntryClass{}).ids ==
          std::vector<std::string>{"my_mod:ember"});
    CHECK(rules->moves.specialtyBlock(3).value_or(dwe::assets::EntryClass{}).ids.empty());

    std::string unknown{originalSectionText()};
    unknown.replace(unknown.find(end), end.size(), end + "  specialty_block_ids: {9: [\"my_mod:ember\"]}\n");
    const auto refused = readEvolutionRules(sectionFrom(unknown));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.contains("specialty 9 has no block"));
}

TEST_CASE("the section check accepts the game pack's section") {
    const auto checked = checkEvolutionSection(sectionFrom(originalSectionText()));
    CHECK_MESSAGE(checked.has_value(), errorOf(checked));
}

TEST_CASE("an unknown strategy is an error at its line, with the known names") {
    const auto checked = checkEvolutionSection(sectionFrom(originalWith("selection: original", "selection: newest")));
    REQUIRE_FALSE(checked.has_value());
    CHECK(checked.error().message.find("unknown evolution selection 'newest'; known: original") != std::string::npos);
    CHECK(checked.error().message.find("line ") != std::string::npos);
}

TEST_CASE("the section reader refuses what the rules could not use") {
    SUBCASE("a divisor of 0") {
        const auto rules = readEvolutionRules(sectionFrom(originalWith("hp_mp_divisor: 10", "hp_mp_divisor: 0")));
        REQUIRE_FALSE(rules.has_value());
        CHECK(rules.error().message.find("hp_mp_divisor must be 1 or more") != std::string::npos);
    }
    SUBCASE("a comparison with two operators") {
        const auto rules = readEvolutionRules(
            sectionFrom(originalWith("tiredness: { equals: 0 }", "tiredness: { equals: 0, at_least: 1 }")));
        REQUIRE_FALSE(rules.has_value());
        CHECK(rules.error().message.find("exactly one of equals and at_least") != std::string::npos);
    }
    SUBCASE("a map by anything but equals") {
        const auto rules =
            readEvolutionRules(sectionFrom(originalWith("map: { equals: mayo02 }", "map: { at_least: 1 }")));
        REQUIRE_FALSE(rules.has_value());
        CHECK(rules.error().message.find("map compares with equals and a map id") != std::string::npos);
    }
    SUBCASE("an unknown requirement") {
        const auto rules =
            readEvolutionRules(sectionFrom(originalWith("tiredness: { equals: 0 }", "sleepiness: { equals: 0 }")));
        REQUIRE_FALSE(rules.has_value());
        CHECK(rules.error().message.find("unknown requirement 'sleepiness'") != std::string::npos);
    }
    SUBCASE("a missing quirk") {
        const auto rules = readEvolutionRules(sectionFrom(originalWith("  rookie_stat_tie_picks_last: true", "")));
        REQUIRE_FALSE(rules.has_value());
    }
}

TEST_CASE("every form and item the rules name must be in the tables") {
    EvolutionRules rules = testing::originalRules();
    rules.fallback.rookieTarget = "nobody";
    const testing::TableFixture fixture;
    const auto evolution = Evolution::create(std::move(rules), fixture.build(), std::make_unique<OriginalSelection>(),
                                             std::make_unique<OriginalScoring>());
    REQUIRE_FALSE(evolution.has_value());
    CHECK(evolution.error().message == "fallback.rookie_target: unknown form 'nobody'");
}

} // namespace dwe::game::evolution
