#include <dwe/game/partner/need_gauges.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <ostream>

// The need meters' gauges (need_gauges.hpp): how far hunger and the toilet have built up, from the
// care rules' own countdowns.
namespace dwe::game::partner {

namespace {

SpeciesCare adultWithMeals(std::array<std::int32_t, 8> hours) {
  SpeciesCare species;
  species.level = Level::Rookie;
  species.hungerHours = hours;
  species.poopInterval = 30;
  return species;
}

CareRules rules() {
  CareRules care;
  care.hunger.babyIntervalHours = 3;
  care.hunger.inTrainingIntervalHours = 4;
  care.quirks.toiletDoublesPoopInterval = true;
  return care;
}

} // namespace

TEST_CASE("need gauges: the meal gap is the level's interval or the gap in the meal hours") {
  const CareRules care = rules();
  SpeciesCare baby;
  baby.level = Level::Baby;
  CHECK(mealIntervalMinutes(baby, care.hunger, 9) == 3 * 60);
  baby.level = Level::InTraining;
  CHECK(mealIntervalMinutes(baby, care.hunger, 9) == 4 * 60);

  const SpeciesCare adult = adultWithMeals({8, 12, 18, -1, 0, 0, 0, 0});
  CHECK(mealIntervalMinutes(adult, care.hunger, 12) == 4 * 60);
  CHECK(mealIntervalMinutes(adult, care.hunger, 18) == 6 * 60);
  // Over midnight: from 18 to 8.
  CHECK(mealIntervalMinutes(adult, care.hunger, 8) == 14 * 60);
  CHECK(mealIntervalMinutes(adultWithMeals({10, -1, 0, 0, 0, 0, 0, 0}), care.hunger, 10) ==
        24 * 60);
}

TEST_CASE("need gauges: hunger fills as the next meal nears and is full while hungry") {
  const CareRules care = rules();
  const SpeciesCare adult = adultWithMeals({8, 12, 18, -1, 0, 0, 0, 0});
  PartnerState state;
  state.hunger.nextHungerHour = 12;
  state.hunger.foodLevel = 4 * 60;
  CHECK(needGaugesOf(state, adult, care).hunger == NeedGauge{.value = 0, .max = 240});
  state.hunger.foodLevel = 60;
  CHECK(needGaugesOf(state, adult, care).hunger == NeedGauge{.value = 180, .max = 240});
  state.hunger.foodLevel = 500;
  CHECK(needGaugesOf(state, adult, care).hunger.value == 0);
  state.hunger.foodLevel = -20;
  CHECK(needGaugesOf(state, adult, care).hunger.value == 240);
  state.hunger.foodLevel = 200;
  state.conditions.set(Condition::Hungry);
  CHECK(needGaugesOf(state, adult, care).hunger == NeedGauge{.value = 240, .max = 240});
}

TEST_CASE("need gauges: the toilet fills over the longest wait and is full while it needs to go") {
  CareRules care = rules();
  const SpeciesCare adult = adultWithMeals({8, -1, 0, 0, 0, 0, 0, 0});
  PartnerState state;
  state.toilet.poopLevel = 60;
  CHECK(needGaugesOf(state, adult, care).toilet == NeedGauge{.value = 0, .max = 60});
  state.toilet.poopLevel = 15;
  CHECK(needGaugesOf(state, adult, care).toilet.value == 45);
  state.conditions.set(Condition::NeedsToilet);
  CHECK(needGaugesOf(state, adult, care).toilet.value == 60);
  care.quirks.toiletDoublesPoopInterval = false;
  state.conditions.clear(Condition::NeedsToilet);
  CHECK(needGaugesOf(state, adult, care).toilet == NeedGauge{.value = 15, .max = 30});
}

TEST_CASE("need gauges: a species with no toilet interval still gives a range") {
  SpeciesCare species = adultWithMeals({8, -1, 0, 0, 0, 0, 0, 0});
  species.poopInterval = 0;
  const PartnerState state;
  CHECK(needGaugesOf(state, species, rules()).toilet.max == 1);
}

} // namespace dwe::game::partner
