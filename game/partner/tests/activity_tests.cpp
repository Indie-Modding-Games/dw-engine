#include <dwe/game/partner/partner.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>

#include "partner_fixture.hpp"

// docs/reference/care.md, "Batallas" and "Descanso": what takes the partner's time in one go.
namespace dwe::game::partner {

namespace {

using testing::has;
using testing::OriginalRuleset;
using testing::Raising;

void finish(Raising& raising, BattleResult result, bool tookDamage = false) {
    raising.partner().finishBattle({.result = result, .tookDamage = tookDamage}, raising.clock());
}

} // namespace

TEST_CASE("care.md battles: a won battle gives happiness +2 and tiredness +5, counts and takes 20 minutes") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    finish(raising, BattleResult::Won);
    CHECK(raising.state().mood.happiness == 52);
    CHECK(raising.state().mood.tiredness == 5);
    CHECK(raising.state().evolution.battles == 1);
    CHECK(raising.clock().time().hour == 8);
    CHECK(raising.clock().time().minute == 20);
    // The meal comes 15 minutes sooner, plus the battle's 20; the toilet two steps.
    CHECK(raising.state().hunger.foodLevel == 1'000'000 - 15 - 20);
    CHECK(raising.state().toilet.poopLevel == 1'000'000 - 2);
}

TEST_CASE("care.md battles: fleeing costs happiness -10, discipline -6 and tires 2 more than a battle") {
    OriginalRuleset original;
    Raising raising(original.rules());
    finish(raising, BattleResult::Fled);
    CHECK(raising.state().mood.happiness == 40);
    CHECK(raising.state().mood.discipline == 44);
    CHECK(raising.state().mood.tiredness == 7);
    CHECK(raising.state().evolution.battles == 0);
}

TEST_CASE("care.md battles: losing costs happiness -30 and discipline -20, and a life") {
    OriginalRuleset original;
    Raising raising(original.rules());
    finish(raising, BattleResult::Lost);
    CHECK(raising.state().mood.happiness == 20);
    CHECK(raising.state().mood.discipline == 30);
    CHECK(raising.state().mood.tiredness == 0);
    CHECK(raising.state().life.lives == 2);
}

TEST_CASE("care.md battles: a battle that used all the MP tires 10 more; the original's division drops any less") {
    OriginalRuleset original;
    const auto tiredness = [&](std::int32_t currentMp) {
        Raising raising(original.rules());
        raising.edit([&](PartnerState& state) { state.stats.currentMp = currentMp; });
        finish(raising, BattleResult::Won);
        return raising.state().mood.tiredness;
    };
    CHECK(tiredness(1000) == 5);
    CHECK(tiredness(500) == 5);
    CHECK(tiredness(0) == 15);
    original.set("care", "quirks.post_battle_mp_tiredness_truncated", "false");
    CHECK(tiredness(500) == 10);
}

TEST_CASE("care.md battles: a hurt partner may come out of a won battle injured; the more tired the likelier") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.edit([](PartnerState& state) {
        state.mood.tiredness = 100;
        state.stats.currentHp = 0;
    });
    finish(raising, BattleResult::Won, true);
    CHECK(raising.state().conditions.has(Condition::Injured));
    CHECK(has(raising.partner().takeEvents(), EventKind::GotInjured));

    Raising rested(original.rules());
    finish(rested, BattleResult::Won, true);
    CHECK_FALSE(rested.state().conditions.has(Condition::Injured));
    Raising unhurt(original.rules());
    unhurt.edit([](PartnerState& state) {
        state.mood.tiredness = 100;
        state.stats.currentHp = 0;
    });
    finish(unhurt, BattleResult::Won, false);
    CHECK_FALSE(unhurt.state().conditions.has(Condition::Injured));
}

TEST_CASE("care.md battles: a battle across midnight adds no day of age in the original") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    raising.skipTo(23, 50);
    finish(raising, BattleResult::Won);
    CHECK(raising.clock().time().hour == 0);
    CHECK(raising.state().life.age == 0);
    CHECK(has(raising.partner().takeEvents(), EventKind::NewDay));

    original.set("care", "quirks.battle_midnight_skips_age", "false");
    Raising fixed(original.rules());
    fixed.skipTo(23, 50);
    finish(fixed, BattleResult::Won);
    CHECK(fixed.state().life.age == 1);
}

TEST_CASE("care.md rest: skipping hours spends them on the needs without moving the clock") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    raising.edit([](PartnerState& state) { state.hunger.energy = 15; });
    raising.partner().skipHours(3, raising.clock());
    CHECK(raising.clock().time().hour == 8);
    CHECK(raising.state().evolution.hours == 3);
    CHECK(raising.state().life.remainingLifetime == 357);
    CHECK(raising.state().hunger.foodLevel == 1'000'000 - 180);
    CHECK(raising.state().toilet.poopLevel == 1'000'000 - 18);
    // The energy goes down once, whatever the hours.
    CHECK(raising.state().hunger.energy == 12);
}

TEST_CASE("care.md rest: a skip across midnight ages the partner") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.skipTo(22, 0);
    raising.partner().skipHours(4, raising.clock());
    CHECK(raising.state().life.age == 1);
    CHECK(has(raising.partner().takeEvents(), EventKind::NewDay));
}

TEST_CASE("care.md rest: a skipped hunger window counts its care mistake, and again when it closes") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    raising.edit([](PartnerState& state) {
        state.hunger.foodLevel = 0;
        state.hunger.energy = 0;
    });
    raising.playFrames(1);
    REQUIRE(raising.state().conditions.has(Condition::Hungry));
    raising.partner().skipHours(2, raising.clock());
    raising.edit([](PartnerState& state) { state.hunger.energy = 0; });
    raising.playFrames(1);
    CHECK(raising.state().evolution.careMistakes == 2);

    original.set("care", "quirks.skipped_hunger_window_counts_twice", "false");
    Raising fixed(original.rules());
    fixed.quiet();
    fixed.edit([](PartnerState& state) {
        state.hunger.foodLevel = 0;
        state.hunger.energy = 0;
    });
    fixed.playFrames(1);
    fixed.partner().skipHours(2, fixed.clock());
    fixed.edit([](PartnerState& state) { state.hunger.energy = 0; });
    fixed.playFrames(1);
    CHECK(fixed.state().evolution.careMistakes == 1);
}

TEST_CASE("care.md rest: a sick partner's skipped hours add sickness rolls in the original") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.edit([](PartnerState& state) { state.conditions.set(Condition::Sick); });
    raising.partner().skipHours(3, raising.clock());
    CHECK(raising.state().health.sicknessTries == 3);
    original.set("care", "quirks.skipped_hours_add_sickness_tries", "false");
    Raising fixed(original.rules());
    fixed.edit([](PartnerState& state) { state.conditions.set(Condition::Sick); });
    fixed.partner().skipHours(3, fixed.clock());
    CHECK(fixed.state().health.sicknessTries == 0);
}

} // namespace dwe::game::partner
