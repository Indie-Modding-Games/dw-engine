#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/partner.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>
#include <string>

#include "partner_fixture.hpp"

// docs/reference/care.md, "Entrenamiento": one test per rule. The test rookie has aptitude 1 (×1
// for HP) and the rookie level (×1), so the HP course gives its bases: HP 80, offense 2.
namespace dwe::game::partner {

namespace {

using testing::has;
using testing::OriginalRuleset;
using testing::Raising;

TrainingRequest hpCourse() {
  return {.place = "trn",
          .course = "hp",
          .slotTenths = 0,
          .gymUpgraded = false,
          .hasTrainingManual = false};
}

StatBlock trained(Raising& raising, const TrainingRequest& request) {
  auto result = raising.partner().train(request, raising.clock());
  REQUIRE_MESSAGE(result.has_value(), testing::errorOf(result));
  return result->gains;
}

} // namespace

TEST_CASE("care.md training: a session gives the course's bases times the multiplier") {
  OriginalRuleset original;
  Raising raising(original.rules());
  const StatBlock gains = trained(raising, hpCourse());
  CHECK(gains[Stat::Hp] == 80);
  CHECK(gains[Stat::Offense] == 2);
  CHECK(raising.state().stats.base[Stat::Hp] == 1080);
  CHECK(raising.state().stats.base[Stat::Offense] == 102);
  CHECK(has(raising.partner().takeEvents(), EventKind::Trained));
}

TEST_CASE("care.md training: the species' aptitude and level change the multiplier") {
  OriginalRuleset original;
  Raising raising(original.rules());
  // Aptitude 1 for offense is ×1.1: 8 × 11 / 10 = 8.
  CHECK(trained(raising, {.place = "trn", .course = "offense"})[Stat::Offense] == 8);
  Raising champion(original.rules(), "greymon");
  // A champion trains at ×1.1.
  CHECK(trained(champion, hpCourse())[Stat::Hp] == 88);
}

TEST_CASE(
    "care.md training: an upgraded gym and ten young sessions each multiply by 1.2, the manual by 1.1") {
  OriginalRuleset original;
  Raising raising(original.rules());
  TrainingRequest upgraded = hpCourse();
  upgraded.gymUpgraded = true;
  CHECK(trained(raising, upgraded)[Stat::Hp] == 96);
  TrainingRequest manual = hpCourse();
  manual.hasTrainingManual = true;
  CHECK(trained(raising, manual)[Stat::Hp] == 88);
  raising.edit([](PartnerState& state) {
    state.training.youngSessions[Stat::Hp] = 10;
    state.mood.tiredness = 0;
  });
  CHECK(trained(raising, hpCourse())[Stat::Hp] == 96);
  // The courses without the bonuses ignore them.
  TrainingRequest plain{.place = "trn", .course = "offense_alt", .gymUpgraded = true};
  CHECK(trained(raising, plain)[Stat::Hp] == 10 * 11 / 10);
}

TEST_CASE(
    "care.md training: the sessions as a baby or in-training count towards the practice bonus") {
  OriginalRuleset original;
  Raising baby(original.rules(), "bota");
  trained(baby, hpCourse());
  CHECK(baby.state().training.youngSessions[Stat::Hp] == 1);
  Raising rookie(original.rules());
  trained(rookie, hpCourse());
  CHECK(rookie.state().training.youngSessions[Stat::Hp] == 0);
  // The second place does not count them.
  Raising elsewhere(original.rules(), "bota");
  trained(elsewhere, {.place = "trn2", .course = "hp_a"});
  CHECK(elsewhere.state().training.youngSessions[Stat::Hp] == 0);
  // Nor do the first place's courses away from the town gym (only map 0x70 counts).
  Raising away(original.rules(), "bota");
  TrainingRequest awayCourse = hpCourse();
  awayCourse.countsPractice = false;
  trained(away, awayCourse);
  CHECK(away.state().training.youngSessions[Stat::Hp] == 0);
}

TEST_CASE("care.md training: the bonus roulette multiplies the gains") {
  OriginalRuleset original;
  Raising raising(original.rules());
  TrainingRequest lucky = hpCourse();
  lucky.slotTenths = 20;
  CHECK(trained(raising, lucky)[Stat::Hp] == 160);
}

TEST_CASE("care.md training: a food's boost multiplies the gains of its stats while it lasts") {
  OriginalRuleset original;
  Raising raising(original.rules());
  FoodEffect boost;
  boost.item = "boost";
  boost.meal = false;
  boost.boost = {.stats = {Stat::Hp}, .value = 20, .hours = 1};
  raising.partner().eat(boost, raising.clock());
  const StatBlock gains = trained(raising, hpCourse());
  CHECK(gains[Stat::Hp] == 160);
  CHECK(gains[Stat::Offense] == 2);
}

TEST_CASE("care.md training: in the original any later food ends the boost") {
  OriginalRuleset original;
  Raising raising(original.rules());
  FoodEffect boost;
  boost.item = "boost";
  boost.meal = false;
  boost.boost = {.stats = {Stat::Hp}, .value = 20, .hours = 1};
  FoodEffect snack;
  snack.item = "snack";
  snack.meal = false;
  raising.partner().eat(boost, raising.clock());
  raising.partner().eat(snack, raising.clock());
  CHECK(trained(raising, hpCourse())[Stat::Hp] == 80);

  original.set("care", "quirks.food_clears_training_boost", "false");
  Raising fixed(original.rules());
  fixed.partner().eat(boost, fixed.clock());
  fixed.partner().eat(snack, fixed.clock());
  CHECK(trained(fixed, hpCourse())[Stat::Hp] == 160);
}

TEST_CASE("care.md training: a tired-out partner gains 1 of each") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) { state.mood.tiredness = 100; });
  const StatBlock gains = trained(raising, hpCourse());
  CHECK(gains[Stat::Hp] == 1);
  CHECK(gains[Stat::Offense] == 1);
}

TEST_CASE("care.md training: the gains stop at 9999 for HP and MP and 999 for the rest") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) {
    state.stats.base[Stat::Hp] = 9990;
    state.stats.base[Stat::Offense] = 998;
  });
  trained(raising, hpCourse());
  CHECK(raising.state().stats.base[Stat::Hp] == 9999);
  CHECK(raising.state().stats.base[Stat::Offense] == 999);
}

TEST_CASE("care.md training: brain training uses the MP aptitude in the original") {
  OriginalRuleset original;
  // The original's tables give MP and brain the same aptitudes; make them differ to see which one
  // is used.
  original.set("training", "aptitude_tenths.brain", "[20, 20, 20, 20, 20]");
  const TrainingRequest brain{.place = "trn", .course = "brain"};
  Raising raising(original.rules());
  // MP aptitude 1: ×0.9.
  CHECK(trained(raising, brain)[Stat::Brain] == 8 * 9 / 10);
  original.set("training", "quirks.brain_training_uses_mp_aptitude", "false");
  Raising fixed(original.rules());
  CHECK(trained(fixed, brain)[Stat::Brain] == 16);
}

TEST_CASE("care.md training: a session takes an hour, tires, costs energy and happiness") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) { state.mood.happiness = 100; });
  trained(raising, hpCourse());
  CHECK(raising.clock().time().hour == 9);
  CHECK(raising.state().mood.tiredness == 10);
  CHECK(raising.state().mood.happiness == 99);
  // The course's 1 and the species' hourly 3.
  CHECK(raising.state().hunger.energy == 15 - 1 - 3);
  CHECK(raising.state().evolution.hours == 1);
  CHECK(raising.state().life.remainingLifetime == 359);
  CHECK(raising.state().hunger.foodLevel == 1'000'000 - 60);
  CHECK(raising.state().toilet.poopLevel == 1'000'000 - 6);
}

TEST_CASE("care.md training: overwork past 70 tiredness costs 3 happiness and 3 discipline") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) { state.mood.tiredness = 65; });
  trained(raising, hpCourse());
  CHECK(raising.state().mood.happiness == 50 - 1 - 3);
  CHECK(raising.state().mood.discipline == 47);
}

TEST_CASE(
    "care.md training: an unknown place or course is an error, and a dead partner does not train") {
  OriginalRuleset original;
  Raising raising(original.rules());
  CHECK_FALSE(
      raising.partner().train({.place = "nowhere", .course = "hp"}, raising.clock()).has_value());
  CHECK_FALSE(
      raising.partner().train({.place = "trn", .course = "luck"}, raising.clock()).has_value());
  raising.edit([](PartnerState& state) { state.life.phase = LifePhase::DiedNaturally; });
  CHECK(testing::errorOf(raising.partner().train(hpCourse(), raising.clock())) ==
        messages::NotAlive);
}

// trn2_reward.c: TRN2_awardMpTrainingGains and TRN2_awardOffenseTrainingGains: one gain each, the
// multiplier without the gym's bonuses (TRN2_calculateTrainingMultiplier), the gain capped at 999
// and the hour's costs of the second place.
TEST_CASE("care.md training: the second place's MP and offense courses give one gain each") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) { state.mood.tiredness = 0; });
  const std::int32_t happiness = raising.state().mood.happiness;
  // MP: 150 at the rookie's ×0.9 for MP (aptitude 1).
  StatBlock gains = trained(raising, {.place = "trn2", .course = "mp"});
  CHECK(gains[Stat::Mp] == 135);
  CHECK(gains[Stat::Defense] == 0);
  CHECK(raising.state().mood.tiredness == 7);
  CHECK(raising.state().mood.happiness == happiness - 4);
  // Offense: 12 at ×1.1.
  gains = trained(raising, {.place = "trn2", .course = "offense"});
  CHECK(gains[Stat::Offense] == 13);
  CHECK(gains[Stat::Speed] == 0);
  CHECK(raising.state().mood.tiredness == 16);
  CHECK(raising.state().mood.happiness == happiness - 5);
  // Neither an upgraded gym nor practice counts there; the manual and the roulette do.
  raising.edit([](PartnerState& state) {
    state.training.youngSessions[Stat::Mp] = 10;
    state.mood.tiredness = 0;
  });
  CHECK(trained(raising, {.place = "trn2", .course = "mp", .gymUpgraded = true})[Stat::Mp] == 135);
  CHECK(trained(raising, {.place = "trn2", .course = "mp", .slotTenths = 20})[Stat::Mp] == 270);
}

TEST_CASE("care.md training: a second-place gain is capped at 999, and 1 when worn out") {
  OriginalRuleset original;
  Raising raising(original.rules());
  FoodEffect boost;
  boost.item = "boost";
  boost.meal = false;
  boost.boost = {.stats = {Stat::Mp}, .value = 100, .hours = 1};
  raising.partner().eat(boost, raising.clock());
  // 150 × (9 × 100) / 100 = 1350.
  CHECK(trained(raising, {.place = "trn2", .course = "mp"})[Stat::Mp] == 999);
  raising.edit([](PartnerState& state) { state.mood.tiredness = 100; });
  CHECK(trained(raising, {.place = "trn2", .course = "offense"})[Stat::Offense] == 1);
}

} // namespace dwe::game::partner
