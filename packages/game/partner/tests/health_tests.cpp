#include <dwe/game/partner/partner.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>

#include "partner_fixture.hpp"

// docs/reference/care.md, "Enfermedad y heridas": one test per rule.
namespace dwe::game::partner {

namespace {

using testing::has;
using testing::OriginalRuleset;
using testing::Raising;

} // namespace

TEST_CASE(
    "care.md sickness: ten sleepy hours start the sickness rolls; the original's chance wraps to a high one") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) { state.sleep.sleepyHoursForSickness = 10; });
  // (1 - 10) × 5 + (0 - 50) = -95, kept in a byte: 161, more than any roll.
  CHECK(has(raising.playHours(1), EventKind::GotSick));
  CHECK(raising.state().health.timesSick == 1);
  CHECK(raising.state().health.sicknessTries == 1);
  CHECK(raising.state().sleep.sleepyHoursForSickness == 0);
}

TEST_CASE("care.md sickness: without the quirk a negative chance never makes the partner sick") {
  OriginalRuleset original;
  original.set("care", "quirks.sickness_chance_wraps", "false");
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) { state.sleep.sleepyHoursForSickness = 10; });
  CHECK_FALSE(has(raising.playHours(3), EventKind::GotSick));
  CHECK(raising.state().health.sicknessTries == 3);
}

TEST_CASE(
    "care.md sickness: a sick partner loses 10 happiness and 5 discipline and gains 5 tiredness each hour") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) {
    state.conditions.set(Condition::Sick);
    state.health.sickHours = 1;
  });
  raising.playHours(1);
  CHECK(raising.state().mood.happiness == 40);
  CHECK(raising.state().mood.discipline == 45);
  CHECK(raising.state().mood.tiredness == 5);
  CHECK(raising.state().health.sickHours == 2);
}

TEST_CASE("care.md sickness: twelve hours sick costs a life") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) {
    state.conditions.set(Condition::Sick);
    state.health.sickHours = 1;
  });
  CHECK_FALSE(has(raising.playHours(10), EventKind::LostLife));
  const std::int32_t lost = testing::valueOf(raising.playHours(1), EventKind::LostLife);
  CHECK(lost == 2);
  CHECK(raising.state().life.lives == 2);
  CHECK(raising.state().health.sickHours == 0);
  CHECK(raising.state().conditions.has(Condition::Sick));
}

TEST_CASE("care.md sickness: with no lives left the partner dies, and passes nothing on") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) {
    state.conditions.set(Condition::Sick);
    state.health.sickHours = 11;
    state.life.lives = 1;
  });
  const auto events = raising.playHours(1);
  const std::int32_t died = testing::valueOf(events, EventKind::Died);
  CHECK(died == 0);
  CHECK(raising.state().life.phase == LifePhase::DiedOtherwise);
  CHECK(raising.state().life.remainingLifetime == 0);
  CHECK_FALSE(raising.partner().alive());
}

TEST_CASE("care.md injuries: twelve hours injured turns into a sickness") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) { state.conditions.set(Condition::Injured); });
  CHECK_FALSE(has(raising.playHours(11), EventKind::GotSick));
  CHECK(has(raising.playHours(1), EventKind::GotSick));
  CHECK_FALSE(raising.state().conditions.has(Condition::Injured));
  CHECK(raising.state().health.injuredHours == 0);
}

TEST_CASE("care.md injuries: the injury waits while the tamer is busy") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) { state.conditions.set(Condition::Injured); });
  const CareContext busy{.tamerIdle = false, .scriptIdle = true, .area = AreaResponse::Neutral};
  CHECK_FALSE(has(raising.playHours(13, busy), EventKind::GotSick));
  CHECK(has(raising.playFrames(1), EventKind::GotSick));
}

TEST_CASE("care.md sickness: ten hours in a disliked area make the partner sick") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  const CareContext disliked{.tamerIdle = true, .scriptIdle = true, .area = AreaResponse::Disliked};
  CHECK_FALSE(has(raising.playHours(10, disliked), EventKind::GotSick));
  CHECK(has(raising.playHours(1, disliked), EventKind::GotSick));
  CHECK(raising.state().mood.areaFrames < 1200);
}

TEST_CASE("care.md sickness: a food may make the partner sick, and clears an injury when it does") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) { state.conditions.set(Condition::Injured); });
  FoodEffect bad;
  bad.item = "rotten";
  bad.meal = false;
  bad.sicknessPercent = 100;
  raising.partner().eat(bad, raising.clock());
  CHECK(has(raising.partner().takeEvents(), EventKind::GotSick));
  CHECK(raising.state().conditions.has(Condition::Sick));
  CHECK_FALSE(raising.state().conditions.has(Condition::Injured));
}

TEST_CASE("care.md cures: a medicine heals what its chances roll; a bandage only injuries") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) {
    state.conditions.set(Condition::Injured);
    state.conditions.set(Condition::Sick);
  });
  const CureEffect bandage{.injuryPercent = 100, .sicknessPercent = 0};
  CHECK_FALSE(raising.partner().cure(bandage));
  CHECK_FALSE(raising.state().conditions.has(Condition::Injured));
  CHECK(raising.state().conditions.has(Condition::Sick));
  const CureEffect medicine{.injuryPercent = 100, .sicknessPercent = 100};
  CHECK(raising.partner().cure(medicine));
  CHECK_FALSE(raising.state().conditions.has(Condition::Sick));
  CHECK(raising.state().health.sickHours == 0);
}

TEST_CASE("care.md cures: a fainted partner takes no medicine") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) {
    state.conditions.set(Condition::Sick);
    state.stats.currentHp = 0;
  });
  CHECK_FALSE(raising.partner().cure({.injuryPercent = 100, .sicknessPercent = 100}));
  CHECK(raising.state().conditions.has(Condition::Sick));
}

TEST_CASE("care.md cures: a poop eater may heal eating a pile") {
  OriginalRuleset original;
  // A pile of 14 or more heals with a chance of 20 %; try a few seeds until one does.
  bool healed = false;
  for (std::uint64_t seed = 1; seed < 40 && !healed; ++seed) {
    Raising raising(original.rules(), "nume", seed);
    raising.edit([](PartnerState& state) { state.conditions.set(Condition::Sick); });
    raising.partner().eatPoop(20);
    healed = !raising.state().conditions.has(Condition::Sick);
  }
  CHECK(healed);
}

} // namespace dwe::game::partner
