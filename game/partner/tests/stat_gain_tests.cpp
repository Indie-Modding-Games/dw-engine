#include <dwe/game/partner/partner.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <ostream>

#include "partner_fixture.hpp"

// The cap of every stat gain (care.limits.stat_gain_max, approved deviation of 2026-10-20): one test per path that
// raises a stat. The test partner starts with HP and MP 1000 and 100 of the rest.
namespace dwe::game::partner {

namespace {

using testing::OriginalRuleset;
using testing::Raising;

// The HP course with every bonus and the best roulette: HP 80 × 1.3 × 10 = 1040, offense 2 × 1.3 × 10 = 20.
TrainingRequest boostedHpCourse() {
    return {.place = "trn", .course = "hp", .slotTenths = 100, .gymUpgraded = true, .hasTrainingManual = true};
}

StatBlock trained(Raising& raising) {
    auto result = raising.partner().train(boostedHpCourse(), raising.clock());
    REQUIRE_MESSAGE(result.has_value(), testing::errorOf(result));
    return result->gains;
}

void dieNaturally(Raising& raising) {
    raising.edit([](PartnerState& state) {
        state.life.remainingLifetime = 1;
        state.mood.happiness = 100;
    });
    raising.playHours(1);
    REQUIRE_FALSE(raising.partner().alive());
}

} // namespace

TEST_CASE("stat gains: a training gain stops at 999") {
    OriginalRuleset original;
    Raising raising(original.rules());
    const StatBlock gains = trained(raising);
    CHECK(gains[Stat::Hp] == 999);
    CHECK(gains[Stat::Offense] == 20);
    CHECK(raising.state().stats.base[Stat::Hp] == 1999);
    CHECK(raising.state().stats.base[Stat::Offense] == 120);
}

TEST_CASE("stat gains: the cap is the ruleset's value") {
    OriginalRuleset original;
    original.set("care", "limits.stat_gain_max", "5000");
    Raising uncapped(original.rules());
    CHECK(trained(uncapped)[Stat::Hp] == 1040);
    original.set("care", "limits.stat_gain_max", "50");
    Raising low(original.rules());
    const StatBlock gains = trained(low);
    CHECK(gains[Stat::Hp] == 50);
    CHECK(gains[Stat::Offense] == 20);
}

TEST_CASE("stat gains: an item's permanent gain stops at 999") {
    OriginalRuleset original;
    Raising raising(original.rules());
    FoodEffect chip;
    chip.item = "chip";
    chip.meal = false;
    chip.permanentGain[Stat::Hp] = 1000;
    chip.permanentGain[Stat::Mp] = 500;
    raising.partner().eat(chip, raising.clock());
    CHECK(raising.state().stats.base[Stat::Hp] == 1999);
    CHECK(raising.state().stats.base[Stat::Mp] == 1500);
}

TEST_CASE("stat gains: an evolution's gains stop at 999, and each stat at its limit") {
    OriginalRuleset original;
    Raising raising(original.rules());
    StatBlock evolution;
    evolution[Stat::Hp] = 1500;
    evolution[Stat::Mp] = 300;
    evolution[Stat::Speed] = 950;
    const StatBlock added = raising.partner().addStatGains(evolution);
    CHECK(added[Stat::Hp] == 999);
    CHECK(added[Stat::Mp] == 300);
    CHECK(added[Stat::Speed] == 950);
    CHECK(raising.state().stats.base[Stat::Hp] == 1999);
    CHECK(raising.state().stats.base[Stat::Mp] == 1300);
    // 100 + 950 is past the speed limit of 999.
    CHECK(raising.state().stats.base[Stat::Speed] == 999);
}

TEST_CASE("stat gains: the stats a reborn baby inherits stop at 999") {
    OriginalRuleset original;
    // 50 % per tamer level: at level 10 the baby would inherit five times the stats.
    original.set("life", "rebirth.inherit_percent_per_tamer_level", "50");
    Raising raising(original.rules());
    raising.quiet();
    dieNaturally(raising);
    REQUIRE(raising.partner().rebirth(raising.clock(), 10).has_value());
    const std::int32_t reborn = testing::valueOf(raising.partner().takeEvents(), EventKind::Reborn);
    const RebirthBaby& baby = raising.rules().life.rebirth.babies.at(static_cast<std::size_t>(reborn));
    // HP 1000 × 5 is capped; offense 100 × 5 is not.
    CHECK(raising.state().stats.base[Stat::Hp] == baby.stats[Stat::Hp] + 999);
    CHECK(raising.state().stats.base[Stat::Offense] == baby.stats[Stat::Offense] + 500);
    CHECK(raising.state().stats.currentHp == raising.state().stats.base[Stat::Hp]);
}

} // namespace dwe::game::partner
