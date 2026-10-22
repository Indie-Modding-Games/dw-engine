#include <dwe/game/partner/partner.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <ostream>

#include "partner_fixture.hpp"

// docs/reference/care.md, "Felicidad y disciplina" and "Cansancio": one test per rule.
namespace dwe::game::partner {

namespace {

using testing::has;
using testing::OriginalRuleset;
using testing::Raising;

} // namespace

TEST_CASE("care.md mood: a partner starts with 50 happiness and 50 discipline") {
  OriginalRuleset original;
  Raising raising(original.rules());
  CHECK(raising.state().mood.happiness == 50);
  CHECK(raising.state().mood.discipline == 50);
}

TEST_CASE("care.md mood: a scold gives discipline +2 and happiness -10") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.partner().scold();
  CHECK(raising.state().mood.discipline == 52);
  CHECK(raising.state().mood.happiness == 40);
}

TEST_CASE(
    "care.md mood: a scold right after a refusal or a poop on the ground gives discipline +8 and costs no "
    "happiness, once") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) { state.mood.scoldBoost = ScoldBoost::Ready; });
  raising.partner().scold();
  CHECK(raising.state().mood.discipline == 58);
  CHECK(raising.state().mood.happiness == 50);
  CHECK(raising.state().mood.scoldBoost == ScoldBoost::Used);
  // The next offer finds the boost used and resets it.
  CHECK_FALSE(raising.partner().refuses({.item = "disk", .check = RefusalCheck::Discipline}));
  CHECK(raising.state().mood.scoldBoost == ScoldBoost::None);
}

TEST_CASE("care.md mood: a scold ends the sulking") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) { state.conditions.set(Condition::Sulking); });
  raising.partner().scold();
  CHECK_FALSE(raising.state().conditions.has(Condition::Sulking));
}

TEST_CASE(
    "care.md mood: scolding a rookie with no discipline at the lowest happiness is told to the session") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) {
    state.mood.discipline = 0;
    state.mood.happiness = -100;
  });
  raising.partner().scold();
  CHECK(has(raising.partner().takeEvents(), EventKind::ScoldedAtRockBottom));
}

TEST_CASE(
    "care.md mood: a praise gives happiness +2 plus a tenth of the discipline, and discipline -5") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.partner().praise();
  CHECK(raising.state().mood.happiness == 57);
  CHECK(raising.state().mood.discipline == 45);
}

TEST_CASE(
    "care.md mood: happiness stays within -100 and 100, discipline and tiredness within 0 and 100") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) {
    state.mood.happiness = 150;
    state.mood.discipline = -5;
    state.mood.tiredness = 130;
  });
  raising.playFrames(1);
  CHECK(raising.state().mood.happiness == 100);
  CHECK(raising.state().mood.discipline == 0);
  CHECK(raising.state().mood.tiredness == 100);
}

TEST_CASE(
    "care.md mood: an unhappy partner with no need may start sulking, and loses stats until it is happy "
    "again") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) {
    state.mood.happiness = -10;
    state.mood.discipline = 0;
  });
  // Checked every 200 frames: random(100) < |happiness| - discipline.
  bool sulking = false;
  for (int check = 0; check < 40 && !sulking; ++check) {
    sulking = has(raising.playFrames(200), EventKind::StartedSulking);
    raising.edit(
        [](PartnerState& state) { state.mood.happiness = std::min(state.mood.happiness, -10); });
  }
  REQUIRE(sulking);
  // 2 % of the stats every 300 frames while it sulks.
  raising.edit([](PartnerState& state) { state.mood.happiness = -100; });
  const std::int32_t hp = raising.state().stats.base[Stat::Hp];
  raising.playFrames(300);
  CHECK(raising.state().stats.base[Stat::Hp] == hp - (hp * 2 / 100));
  // +1 happiness every 15 frames; at 0 it is over.
  raising.edit([](PartnerState& state) { state.mood.happiness = -1; });
  raising.playFrames(15);
  CHECK_FALSE(raising.state().conditions.has(Condition::Sulking));
}

TEST_CASE("care.md mood: a disciplined partner does not sulk") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) {
    state.mood.happiness = -50;
    state.mood.discipline = 50;
  });
  CHECK_FALSE(has(raising.playHours(1), EventKind::StartedSulking));
}

TEST_CASE(
    "care.md mood: each hour in a liked area gives happiness +1 and tiredness -2; a disliked one the "
    "opposite") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) { state.mood.tiredness = 10; });
  raising.partner().enterMap();
  raising.playFrames(1201, {.tamerIdle = true, .area = AreaResponse::Liked});
  CHECK(raising.state().mood.happiness == 51);
  CHECK(raising.state().mood.tiredness == 8);
  raising.playFrames(1200, {.tamerIdle = true, .area = AreaResponse::Disliked});
  CHECK(raising.state().mood.happiness == 50);
  CHECK(raising.state().mood.tiredness == 9);
}

TEST_CASE("care.md tiredness: at 80 the partner is tired and loses 2 happiness every 100 frames") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) { state.mood.tiredness = 80; });
  CHECK(has(raising.playFrames(1), EventKind::BecameTired));
  raising.playFrames(200);
  CHECK(raising.state().mood.happiness <= 46);
  raising.edit([](PartnerState& state) { state.mood.tiredness = 79; });
  raising.playFrames(1);
  CHECK_FALSE(raising.state().conditions.has(Condition::Tired));
}

TEST_CASE("care.md tiredness: a meal or a rest takes tiredness off") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) { state.mood.tiredness = 30; });
  FoodEffect drink;
  drink.item = "drink";
  drink.meal = false;
  drink.tirednessRelief = 50;
  raising.partner().eat(drink, raising.clock());
  CHECK(raising.state().mood.tiredness == 0);
}

TEST_CASE(
    "care.md year: a new year at 100 happiness may raise the tamer's level, below 0 lower it") {
  OriginalRuleset original;
  original.set("care", "year.happy_tamer_level_percent", "100");
  original.set("care", "year.unhappy_tamer_level_percent", "100");
  const auto yearEvents = [&](std::int32_t happiness) {
    Raising raising(original.rules());
    const std::int64_t yearFrames = 30LL * 24 * 60 * 20;
    // To the last minute of the year, without playing it.
    raising.clock().advanceFrames(yearFrames - 20 - raising.clock().elapsedFrames());
    raising.edit([&](PartnerState& state) {
      state.mood.happiness = happiness;
      state.mood.discipline = 100;
      state.life.remainingLifetime = 100;
    });
    return raising.playMinutes(1);
  };
  const std::int32_t up = testing::valueOf(yearEvents(100), EventKind::TamerLevelChanged);
  CHECK(up == 1);
  const std::int32_t down = testing::valueOf(yearEvents(-1), EventKind::TamerLevelChanged);
  CHECK(down == -1);
  CHECK_FALSE(has(yearEvents(50), EventKind::TamerLevelChanged));
}

} // namespace dwe::game::partner
