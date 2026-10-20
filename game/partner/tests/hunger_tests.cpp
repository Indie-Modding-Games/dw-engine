#include <dwe/game/partner/partner.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>

#include "partner_fixture.hpp"

// docs/reference/care.md, "Hambre": one test per rule.
namespace dwe::game::partner {

namespace {

using testing::has;
using testing::OriginalRuleset;
using testing::Raising;

FoodEffect meal(std::int32_t energy) {
    FoodEffect food;
    food.item = "plain";
    food.energy = energy;
    return food;
}

} // namespace

TEST_CASE("care.md hunger: a rookie is hungry at the meal hours of its species") {
    OriginalRuleset original;
    Raising raising(original.rules(), "agu");
    // The game starts at 8:00; the test species eats at 8, 13 and 19.
    CHECK(raising.state().hunger.nextHungerHour == 13);
    CHECK_FALSE(has(raising.playUntil(12, 59), EventKind::BecameHungry));
    CHECK(has(raising.playMinutes(1), EventKind::BecameHungry));
}

TEST_CASE("care.md hunger: a baby is hungry every 2 hours and an in-training partner every 3") {
    OriginalRuleset original;
    Raising baby(original.rules(), "bota");
    CHECK(baby.state().hunger.nextHungerHour == 10);
    CHECK(baby.state().hunger.foodLevel == 120);
    Raising inTraining(original.rules(), "koro");
    CHECK(inTraining.state().hunger.nextHungerHour == 9);
    CHECK(inTraining.state().hunger.foodLevel == 60);
}

TEST_CASE("care.md hunger: an unfed window of 90 minutes is a care mistake") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    raising.edit([](PartnerState& state) {
        state.hunger.foodLevel = 0;
        state.hunger.energy = 0;
    });
    auto events = raising.playMinutes(89);
    CHECK(has(events, EventKind::BecameHungry));
    CHECK_FALSE(has(events, EventKind::HungerWindowClosed));
    events = raising.playMinutes(2);
    const std::int32_t closed = testing::valueOf(events, EventKind::HungerWindowClosed);
    CHECK(closed == 0);
    CHECK(raising.state().evolution.careMistakes == 1);
    CHECK_FALSE(raising.state().conditions.has(Condition::Hungry));
}

TEST_CASE("care.md hunger: energy falls each hour by the species' use, and at 0 the weight falls every 10 minutes") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    raising.edit([](PartnerState& state) { state.hunger.energy = 10; });
    raising.playHours(1);
    CHECK(raising.state().hunger.energy == 7);
    raising.edit([](PartnerState& state) {
        state.hunger.energy = 0;
        state.weight = 20;
    });
    raising.playHours(1);
    CHECK(raising.state().weight == 14);
    CHECK(raising.state().hunger.emptyStomachMinutes == 60);
}

TEST_CASE("care.md hunger: a long tiredness brings the meal 3 minutes closer each hour") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    raising.edit([](PartnerState& state) {
        state.hunger.foodLevel = 300;
        state.mood.tiredness = 60;
    });
    raising.playHours(1);
    CHECK(raising.state().hunger.foodLevel == 300 - 60 - 3);
}

TEST_CASE("care.md hunger: a hungry partner that eats enough is fed: happiness +5, discipline +1") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.quiet();
    raising.edit([](PartnerState& state) {
        state.hunger.foodLevel = 0;
        state.hunger.energy = 0;
    });
    raising.playMinutes(10);
    REQUIRE(raising.state().conditions.has(Condition::Hungry));
    const std::int32_t happiness = raising.state().mood.happiness;
    const std::int32_t discipline = raising.state().mood.discipline;
    raising.partner().eat(meal(10), raising.clock());
    CHECK(raising.state().conditions.has(Condition::Hungry));
    raising.partner().eat(meal(10), raising.clock());
    CHECK_FALSE(raising.state().conditions.has(Condition::Hungry));
    CHECK(raising.state().mood.happiness == happiness + 5);
    CHECK(raising.state().mood.discipline == discipline + 1);
    CHECK(has(raising.partner().takeEvents(), EventKind::HungerWindowClosed));
    // The next meal is the one after the meal last set (13:00 here, set at the start): the original searches from it.
    CHECK(raising.state().hunger.nextHungerHour == 19);
}

TEST_CASE("care.md hunger: feeding a partner that is not hungry costs happiness -3 and discipline -2") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.partner().eat(meal(1), raising.clock());
    CHECK(raising.state().mood.happiness == 47);
    CHECK(raising.state().mood.discipline == 48);
    // Only a meal counts: another item that goes through the food rules leaves the mood alone.
    FoodEffect chip = meal(1);
    chip.meal = false;
    raising.partner().eat(chip, raising.clock());
    CHECK(raising.state().mood.happiness == 47);
}

TEST_CASE("care.md hunger: the favourite food gives 40 % more energy and +2 happiness") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.edit([](PartnerState& state) { state.hunger.energy = 0; });
    FoodEffect favorite = meal(10);
    favorite.item = "meat";
    favorite.meal = false;
    raising.partner().eat(favorite, raising.clock());
    CHECK(raising.state().hunger.energy == 14);
    CHECK(raising.state().mood.happiness == 52);
}

TEST_CASE("care.md hunger: the energy has a cap per species") {
    OriginalRuleset original;
    Raising raising(original.rules());
    raising.partner().eat(meal(1000), raising.clock());
    CHECK(raising.state().hunger.energy == 60);
}

TEST_CASE("care.md hunger: some items are refused less the more discipline the partner has") {
    OriginalRuleset original;
    Raising raising(original.rules());
    const ItemOffer offer{.item = "disk", .check = RefusalCheck::Discipline};
    auto refusals = [&](std::int32_t discipline) {
        std::int32_t refused = 0;
        for (int offerCount = 0; offerCount < 200; ++offerCount) {
            raising.edit([&](PartnerState& state) {
                state.mood.discipline = discipline;
                state.mood.scoldBoost = ScoldBoost::None;
            });
            refused += raising.partner().refuses(offer) ? 1 : 0;
        }
        return refused;
    };
    // random(100) < 110 - (discipline + 10..19).
    CHECK(refusals(100) == 0);
    CHECK(refusals(0) > 170);
    CHECK(raising.state().mood.scoldBoost == ScoldBoost::Ready);
    CHECK(raising.partner().refuses({.item = "evolution", .check = RefusalCheck::Always}));
}

TEST_CASE("care.md hunger: a partner that is not hungry refuses its favourite food 20 % of the times") {
    OriginalRuleset original;
    Raising raising(original.rules());
    const ItemOffer favorite{.item = "meat"};
    std::int32_t refused = 0;
    for (int offerCount = 0; offerCount < 1000; ++offerCount) {
        raising.edit([](PartnerState& state) { state.hunger.refusedFavorite = false; });
        refused += raising.partner().refuses(favorite) ? 1 : 0;
    }
    CHECK(refused > 150);
    CHECK(refused < 250);
    // Once refused, it refuses every food until it is scolded.
    raising.edit([](PartnerState& state) { state.hunger.refusedFavorite = true; });
    CHECK(raising.partner().refuses({.item = "plain"}));
    raising.partner().scold();
    CHECK_FALSE(raising.partner().refuses({.item = "plain"}));
}

} // namespace dwe::game::partner
