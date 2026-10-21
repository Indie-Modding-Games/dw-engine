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

// A won battle that changes nothing but the clock, as long as the battle section says.
BattleConsequences wonBattle(bool hourCrossed) {
    return {.result = BattleResult::Won,
            .injured = false,
            .mood = {},
            .battles = 0,
            .foodChange = 0,
            .livesLost = 0,
            .elapsedMinutes = 20,
            .hourCrossed = hourCrossed,
            .lifetimeHoursLost = 0};
}

} // namespace

TEST_CASE("care.md battles: a battle across midnight adds no day of age in the original") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    raising.skipTo(23, 50);
    raising.partner().applyBattle(wonBattle(true), raising.clock());
    CHECK(raising.clock().time().hour == 0);
    CHECK(raising.state().life.age == 0);
    CHECK(has(raising.partner().takeEvents(), EventKind::NewDay));

    original.set("care", "quirks.battle_midnight_skips_age", "false");
    Raising fixed(original.rules());
    fixed.skipTo(23, 50);
    fixed.partner().applyBattle(wonBattle(true), fixed.clock());
    CHECK(fixed.state().life.age == 1);
}

TEST_CASE("care.md battles: a battle's minutes bring the meal closer and move the toilet one step per poop step") {
    // tamer.c: advanceBattleTime: two toilet steps for its 20 minutes, one per care.toilet.poop_step_minutes.
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    raising.partner().applyBattle(wonBattle(false), raising.clock());
    CHECK(raising.clock().time().minute == 20);
    CHECK(raising.state().hunger.foodLevel == 1'000'000 - 20);
    CHECK(raising.state().toilet.poopLevel == 1'000'000 - 2);
}

TEST_CASE("care.md battles: a battle the battle system settled applies its numbers, not the partner's rules") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    const std::int32_t lifetime = raising.state().life.remainingLifetime;
    raising.partner().applyBattle({.result = BattleResult::Won,
                                   .injured = true,
                                   .mood = {.happiness = 7, .discipline = -3, .tiredness = 4},
                                   .battles = 1,
                                   .foodChange = -9,
                                   .livesLost = 0,
                                   .elapsedMinutes = 30,
                                   .hourCrossed = false,
                                   .lifetimeHoursLost = 0},
                                  raising.clock());
    CHECK(raising.state().mood.happiness == 57);
    CHECK(raising.state().mood.tiredness == 4);
    CHECK(raising.state().evolution.battles == 1);
    CHECK(raising.state().conditions.has(Condition::Injured));
    CHECK(has(raising.partner().takeEvents(), EventKind::GotInjured));
    CHECK(raising.clock().time().minute == 30);
    CHECK(raising.state().hunger.foodLevel == 1'000'000 - 9 - 30);
    CHECK(raising.state().toilet.poopLevel == 1'000'000 - 3);
    CHECK(raising.state().life.remainingLifetime == lifetime);
}

TEST_CASE("care.md battles: a settled battle that ends an hour costs its lifetime hours and runs the hourly counters") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    raising.skipTo(9, 50);
    raising.edit([](PartnerState& state) { state.conditions.set(Condition::Injured); });
    const PartnerState before = raising.state();
    raising.partner().applyBattle({.result = BattleResult::Lost,
                                   .injured = false,
                                   .mood = {.happiness = -30, .discipline = -20, .tiredness = 0},
                                   .battles = 0,
                                   .foodChange = 0,
                                   .livesLost = 1,
                                   .elapsedMinutes = 20,
                                   .hourCrossed = true,
                                   .lifetimeHoursLost = 1},
                                  raising.clock());
    CHECK(raising.clock().time().hour == 10);
    CHECK(raising.state().life.remainingLifetime == before.life.remainingLifetime - 1);
    CHECK(raising.state().health.injuredHours == before.health.injuredHours + 1);
    CHECK(raising.state().life.lives == before.life.lives - 1);
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

TEST_CASE("care.md rest: in the original, skipped hours check the unhappiness only at midnight") {
    // Happiness 30 loses (30 - 80) / -50 + 1 = 2 hours of life at each check.
    const auto unhappy = [](PartnerState& state) { state.mood.happiness = 30; };
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    raising.edit(unhappy);
    // From 8:00, nine hours cross 12:00 and 16:00, but (8 + 9 % 4) is not 0.
    std::int32_t before = raising.state().life.remainingLifetime;
    raising.partner().skipHours(9, raising.clock());
    CHECK(raising.state().life.remainingLifetime == before - 9);
    // From midnight, eight hours: one check, although they cross two multiples of four.
    raising.skipTo(0, 0);
    before = raising.state().life.remainingLifetime;
    raising.partner().skipHours(8, raising.clock());
    CHECK(raising.state().life.remainingLifetime == before - 8 - 2);

    original.set("care", "quirks.skipped_hours_unhappiness_only_at_midnight", "false");
    Raising fixed(original.rules());
    fixed.quiet();
    fixed.edit(unhappy);
    // From 8:00, nine hours cross 12:00 and 16:00: two checks.
    before = fixed.state().life.remainingLifetime;
    fixed.partner().skipHours(9, fixed.clock());
    CHECK(fixed.state().life.remainingLifetime == before - 9 - 4);
    // From midnight, three hours cross none.
    fixed.skipTo(0, 0);
    before = fixed.state().life.remainingLifetime;
    fixed.partner().skipHours(3, fixed.clock());
    CHECK(fixed.state().life.remainingLifetime == before - 3);
    // From 3:00, one hour crosses 4:00.
    fixed.skipTo(3, 0);
    before = fixed.state().life.remainingLifetime;
    fixed.partner().skipHours(1, fixed.clock());
    CHECK(fixed.state().life.remainingLifetime == before - 1 - 2);
}

} // namespace dwe::game::partner
