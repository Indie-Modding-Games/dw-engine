#include <dwe/game/partner/partner.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>

#include "partner_fixture.hpp"

// docs/reference/care.md, "Sueño": one test per rule.
namespace dwe::game::partner {

namespace {

using testing::has;
using testing::OriginalRuleset;
using testing::Raising;

// The test rookie sleeps on the original's pattern 2: sleepy at 22:00, awake at 7:00.
constexpr std::int32_t SleepyHour = 22;
constexpr std::int32_t WakeHour = 7;

} // namespace

TEST_CASE("care.md sleep: a rookie follows the sleep pattern of its species") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  CHECK(raising.state().sleep.sleepyHour == SleepyHour);
  CHECK(raising.state().sleep.wakeHour == WakeHour);
  CHECK(raising.state().sleep.awakeCuts == 15 * 6);
  CHECK_FALSE(has(raising.playUntil(21, 59), EventKind::BecameSleepy));
  CHECK(has(raising.playMinutes(1), EventKind::BecameSleepy));
  CHECK(raising.partner().canSleep());
}

TEST_CASE(
    "care.md sleep: a baby sleeps 3 hours after it starts and an in-training partner 7, for their pattern's "
    "hours") {
  OriginalRuleset original;
  Raising baby(original.rules(), "bota");
  CHECK(baby.state().sleep.sleepyHour == 11);
  CHECK(baby.state().sleep.wakeHour == 12);
  Raising inTraining(original.rules(), "koro");
  CHECK(inTraining.state().sleep.sleepyHour == 15);
  CHECK(inTraining.state().sleep.wakeHour == 18);
}

TEST_CASE("care.md sleep: tiredness brings bedtime forward a cut of 10 minutes at a time") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  // 60 of tiredness adds 18 a minute towards the 180 of a cut.
  raising.edit([](PartnerState& state) { state.mood.tiredness = 60; });
  raising.playMinutes(9);
  CHECK(raising.state().sleep.awakeCuts == 90);
  raising.playMinutes(1);
  CHECK(raising.state().sleep.awakeCuts == 89);
  CHECK(raising.state().sleep.tirednessCounter == 0);
  CHECK(raising.state().sleep.sleepyHour == SleepyHour - 1);
  CHECK(raising.state().sleep.sleepyMinute == 50);
}

TEST_CASE(
    "care.md sleep: a sleepy adult loses 2 happiness and 4 discipline each hour it stays up") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.playUntil(SleepyHour, 0);
  const MoodState before = raising.state().mood;
  raising.playHours(1);
  CHECK(raising.state().mood.happiness == before.happiness - 2);
  CHECK(raising.state().mood.discipline == before.discipline - 4);
}

TEST_CASE(
    "care.md sleep: going to bed jumps to the wake hour, ages the partner and protects the first hour") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.playUntil(SleepyHour, 0);
  const std::int32_t evolutionHours = raising.state().evolution.hours;
  raising.partner().sleep(raising.clock(), {});
  const auto events = raising.partner().takeEvents();
  const std::int32_t slept = testing::valueOf(events, EventKind::Slept);
  CHECK(slept == 9);
  CHECK(has(events, EventKind::NewDay));
  CHECK(raising.clock().time().hour == WakeHour);
  CHECK(raising.clock().time().minute == 0);
  CHECK(raising.state().life.age == 1);
  CHECK(raising.state().life.protectedHour == WakeHour);
  CHECK(raising.state().evolution.hours == evolutionHours + 9);
  CHECK_FALSE(raising.state().conditions.has(Condition::Sleepy));
  CHECK(raising.state().sleep.sleepyHour == SleepyHour);
}

TEST_CASE("care.md sleep: a partner that is not sleepy does not go to bed") {
  OriginalRuleset original;
  Raising raising(original.rules());
  CHECK_FALSE(raising.partner().canSleep());
  raising.partner().sleep(raising.clock(), {});
  CHECK(raising.clock().time().hour == 8);
  CHECK(raising.partner().takeEvents().empty());
}

TEST_CASE("care.md sleep: a full night heals 70 to 79 % of HP and MP and 80 to 99 % of tiredness") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.playUntil(SleepyHour, 0);
  raising.edit([](PartnerState& state) {
    state.stats.currentHp = 0;
    state.stats.currentMp = 0;
    state.mood.tiredness = 50;
    state.weight = 20;
  });
  raising.partner().sleep(raising.clock(), {});
  CHECK(raising.state().stats.currentHp >= 700);
  CHECK(raising.state().stats.currentHp <= 790);
  CHECK(raising.state().stats.currentMp == raising.state().stats.currentHp);
  CHECK(raising.state().mood.tiredness <= 10);
  // The weight falls by a tenth of the species' weight.
  CHECK(raising.state().weight == 18);
}

TEST_CASE("care.md sleep: a pillow heals 20 % more") {
  OriginalRuleset original;
  const auto healed = [&](bool pillow) {
    Raising raising(original.rules());
    raising.quiet();
    raising.playUntil(SleepyHour, 0);
    raising.edit([](PartnerState& state) { state.stats.currentHp = 0; });
    raising.partner().sleep(raising.clock(), {.hasPillow = pillow, .area = AreaResponse::Neutral});
    return raising.state().stats.currentHp;
  };
  const std::int32_t plain = healed(false);
  // The same seed rolls the same heal.
  CHECK(healed(true) == plain * 120 / 100);
}

TEST_CASE("care.md sleep: a partner put to bed late sleeps the hours left to its wake hour") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.playUntil(2, 0);
  raising.partner().sleep(raising.clock(), {});
  const std::int32_t slept = testing::valueOf(raising.partner().takeEvents(), EventKind::Slept);
  CHECK(slept == 5);
}

TEST_CASE(
    "care.md sleep: a partner never put to bed misses its sleep at the wake hour: a care mistake") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.playUntil(SleepyHour, 0);
  CHECK_FALSE(has(raising.playUntil(6, 59), EventKind::CareMistake));
  const auto events = raising.playMinutes(1);
  const std::int32_t mistake = testing::valueOf(events, EventKind::CareMistake);
  CHECK(mistake == static_cast<std::int32_t>(CareMistakeCause::MissedSleep));
  CHECK_FALSE(raising.state().conditions.has(Condition::Sleepy));
  CHECK(raising.state().sleep.missedSleepHours == 9);
  // The hours sleepy count towards the sickness rolls until the partner sleeps.
  CHECK(raising.state().sleep.sleepyHoursForSickness == 9);
}

TEST_CASE("care.md sleep: waking up needing the toilet leaves a pile, but no care mistake") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.playUntil(SleepyHour, 0);
  raising.edit([](PartnerState& state) {
    state.conditions.set(Condition::NeedsToilet);
    state.toilet.urgeFrames = 100;
  });
  raising.partner().sleep(raising.clock(), {});
  const auto events = raising.partner().takeEvents();
  CHECK(has(events, EventKind::PoopedOnGround));
  CHECK_FALSE(has(events, EventKind::CareMistake));
  CHECK(raising.state().toilet.poopLevel == 18);
  CHECK_FALSE(raising.state().conditions.has(Condition::NeedsToilet));
}

TEST_CASE("care.md sleep: a sick partner wakes up worse and counts the night as sick hours") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.playUntil(SleepyHour, 0);
  raising.edit([](PartnerState& state) {
    state.conditions.set(Condition::Sick);
    state.health.sickHours = 1;
    state.mood.happiness = 50;
    state.mood.discipline = 50;
  });
  raising.partner().sleep(raising.clock(), {});
  CHECK(raising.state().mood.happiness == 30);
  CHECK(raising.state().mood.discipline == 40);
  CHECK(raising.state().health.sickHours == 10);
}

TEST_CASE(
    "care.md sleep: a partner that sleeps hungry wakes up fed, and keeps its weight in the original") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.playUntil(SleepyHour, 0);
  raising.edit([](PartnerState& state) {
    state.conditions.set(Condition::Hungry);
    state.weight = 30;
  });
  raising.partner().sleep(raising.clock(), {});
  CHECK_FALSE(raising.state().conditions.has(Condition::Hungry));
  CHECK(raising.state().weight == 28);

  original.set("care", "quirks.sleeping_hungry_keeps_weight", "false");
  Raising fixed(original.rules());
  fixed.quiet();
  fixed.playUntil(SleepyHour, 0);
  fixed.edit([](PartnerState& state) {
    state.conditions.set(Condition::Hungry);
    state.weight = 30;
  });
  fixed.partner().sleep(fixed.clock(), {});
  CHECK(fixed.state().weight == 27);
}

TEST_CASE(
    "care.md sleep: the original adds the pattern's hours to the evolution timer, however long the sleep") {
  OriginalRuleset original;
  original.set("care", "quirks.sleep_adds_default_hours_to_evolution", "false");
  Raising raising(original.rules());
  raising.quiet();
  raising.playUntil(2, 0);
  const std::int32_t hours = raising.state().evolution.hours;
  raising.partner().sleep(raising.clock(), {});
  CHECK(raising.state().evolution.hours == hours + 5);
}

} // namespace dwe::game::partner
