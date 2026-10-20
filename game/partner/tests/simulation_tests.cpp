#include <dwe/game/partner/partner.hpp>
#include <dwe/game/partner/state_yaml.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>
#include <set>
#include <vector>

#include "partner_fixture.hpp"

// Time: a day advanced at once is a day played, and a fixed seed gives the same partner.
namespace dwe::game::partner {

namespace {

using testing::OriginalRuleset;
using testing::Raising;

constexpr std::int64_t FramesPerDay = 24LL * 60 * 20;

// A tamer that answers every need as soon as it shows, minute by minute.
void careFor(Raising& raising, std::int32_t days) {
    FoodEffect meal;
    meal.item = "meat";
    meal.energy = 20;
    for (std::int64_t minute = 0; minute < days * 24LL * 60 && raising.partner().alive(); ++minute) {
        raising.playMinutes(1);
        const Conditions& conditions = raising.state().conditions;
        if (conditions.has(Condition::Hungry)) {
            raising.partner().eat(meal, raising.clock());
        }
        if (conditions.has(Condition::NeedsToilet)) {
            raising.partner().useToilet();
        }
        if (conditions.has(Condition::Sleepy)) {
            raising.partner().sleep(raising.clock(), {});
        }
        if (conditions.has(Condition::Sick)) {
            raising.partner().cure({.injuryPercent = 100, .sicknessPercent = 100});
        }
        static_cast<void>(raising.partner().takeEvents());
    }
}

} // namespace

TEST_CASE("time: three days advanced at once, by hours or frame by frame leave the same partner and events") {
    OriginalRuleset original;
    Raising atOnce(original.rules());
    Raising byHours(original.rules());
    Raising byFrames(original.rules());
    const std::vector<PartnerEvent> onceEvents = atOnce.playFrames(3 * FramesPerDay);
    std::vector<PartnerEvent> hourEvents;
    for (int hour = 0; hour < 3 * 24; ++hour) {
        for (const PartnerEvent& event : byHours.playHours(1)) {
            hourEvents.push_back(event);
        }
    }
    std::vector<PartnerEvent> frameEvents;
    for (std::int64_t frame = 0; frame < 3 * FramesPerDay; ++frame) {
        for (const PartnerEvent& event : byFrames.playFrames(1)) {
            frameEvents.push_back(event);
        }
    }
    CHECK(atOnce.state() == byHours.state());
    CHECK(atOnce.state() == byFrames.state());
    CHECK(onceEvents == hourEvents);
    CHECK(onceEvents == frameEvents);
    CHECK(atOnce.state().life.age == 3);
    CHECK(testing::count(onceEvents, EventKind::NewDay) == 3);
}

TEST_CASE("time: eight days cared for with the same seed raise the same partner") {
    OriginalRuleset original;
    Raising first(original.rules(), "bota", 7);
    Raising second(original.rules(), "bota", 7);
    careFor(first, 8);
    careFor(second, 8);
    CHECK(first.state() == second.state());
    // The nights slept add their days on top of the days played.
    CHECK(first.state().life.age >= 8);
    // The saved state comes back the same.
    const auto restored = fromYaml(toYaml(first.state()));
    REQUIRE_MESSAGE(restored.has_value(), testing::errorOf(restored));
    CHECK(*restored == first.state());
}

TEST_CASE("time: different seeds roll differently") {
    OriginalRuleset original;
    std::set<std::int32_t> heals;
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
        Raising raising(original.rules(), "agu", seed);
        raising.quiet();
        raising.playUntil(22, 0);
        raising.edit([](PartnerState& state) { state.stats.currentHp = 0; });
        raising.partner().sleep(raising.clock(), {});
        heals.insert(raising.state().stats.currentHp);
    }
    CHECK(heals.size() > 1);
}

} // namespace dwe::game::partner
