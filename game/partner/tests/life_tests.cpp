#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/game/partner/strategies.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <ostream>
#include <span>

#include "partner_fixture.hpp"

// docs/reference/care.md, "Vida, muerte y renacimiento": one test per rule.
namespace dwe::game::partner {

namespace {

using testing::has;
using testing::OriginalRuleset;
using testing::Raising;

// A rebirth that always brings back the first baby, to show that the choice is a strategy.
class FirstBaby final : public RebirthChoice {
public:
  [[nodiscard]] std::size_t pick(std::span<const RebirthBaby> /*babies*/,
                                 const PartnerState& /*deceased*/,
                                 ruleset::Random& /*random*/) const override {
    return 0;
  }
};

void dieNaturally(Raising& raising) {
  raising.edit([](PartnerState& state) {
    state.life.remainingLifetime = 1;
    state.mood.happiness = 100;
  });
  raising.playHours(1);
  REQUIRE_FALSE(raising.partner().alive());
}

// A lost battle as the battle section settles it in the original (tamer.c: startBattle).
void loseBattle(Raising& raising) {
  raising.partner().applyBattle({.result = BattleResult::Lost,
                                 .injured = false,
                                 .mood = {.happiness = -30, .discipline = -20, .tiredness = 0},
                                 .battles = 0,
                                 .foodChange = 0,
                                 .livesLost = 1,
                                 .elapsedMinutes = 20,
                                 .hourCrossed = false,
                                 .lifetimeHoursLost = 0},
                                raising.clock());
}

} // namespace

TEST_CASE(
    "care.md life: a partner starts with 360 hours of life and 3 lives, and spends an hour each hour") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  CHECK(raising.state().life.remainingLifetime == 360);
  CHECK(raising.state().life.lives == 3);
  raising.edit([](PartnerState& state) { state.mood.happiness = 100; });
  raising.playHours(5);
  CHECK(raising.state().life.remainingLifetime == 355);
  CHECK(raising.state().evolution.hours == 5);
}

TEST_CASE(
    "care.md life: every 4 hours an unhappy partner loses (80 - happiness) / 50 + 1 hours more") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) {
    state.mood.happiness = -100;
    state.mood.discipline = 100;
  });
  // 8:00 to 12:00: four hours, and at 12:00 (−100 − 80) / −50 + 1 = 4 more.
  raising.playHours(4);
  CHECK(raising.state().life.remainingLifetime == 360 - 4 - 4);
}

TEST_CASE("care.md life: the day turns at midnight: the partner is a day older") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  const auto events = raising.playUntil(0, 0);
  const std::int32_t day = testing::valueOf(events, EventKind::NewDay);
  CHECK(day == 1);
  CHECK(raising.state().life.age == 1);
}

TEST_CASE("care.md death: with no lifetime left the partner dies a natural death") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) {
    state.life.remainingLifetime = 1;
    state.mood.happiness = 100;
  });
  CHECK_FALSE(has(raising.playMinutes(59), EventKind::Died));
  const std::int32_t died = testing::valueOf(raising.playMinutes(1), EventKind::Died);
  CHECK(died == 1);
  CHECK(raising.state().life.phase == LifePhase::DiedNaturally);
  // A dead partner's needs stop.
  const PartnerState dead = raising.state();
  CHECK(raising.playHours(5).empty());
  CHECK(raising.state() == dead);
}

TEST_CASE("care.md death: death waits while the tamer is busy and in the hour after waking up") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) { state.life.remainingLifetime = 0; });
  CHECK_FALSE(has(raising.playMinutes(5, {.tamerIdle = false, .area = AreaResponse::Neutral}),
                  EventKind::Died));
  raising.edit([](PartnerState& state) { state.life.protectedHour = 8; });
  CHECK_FALSE(has(raising.playMinutes(30), EventKind::Died));
  CHECK(has(raising.playUntil(9, 0), EventKind::Died));
}

TEST_CASE("care.md death: a lost battle costs a life; the last one kills the partner") {
  OriginalRuleset original;
  Raising raising(original.rules());
  loseBattle(raising);
  CHECK(raising.state().life.lives == 2);
  raising.edit([](PartnerState& state) { state.life.lives = 1; });
  loseBattle(raising);
  CHECK(raising.state().life.phase == LifePhase::DiedOtherwise);
}

TEST_CASE(
    "care.md rebirth: one of the four babies, with a part of the stats of a partner that died naturally") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  dieNaturally(raising);
  const StatBlock before = raising.state().stats.base;
  REQUIRE(raising.partner().rebirth(raising.clock(), 5).has_value());
  const std::int32_t reborn = testing::valueOf(raising.partner().takeEvents(), EventKind::Reborn);
  const RebirthBaby& baby =
      raising.rules().life.rebirth.babies.at(static_cast<std::size_t>(reborn));
  CHECK(raising.state().species == baby.digimon);
  for (const Stat stat : AllStats) {
    CAPTURE(static_cast<int>(stat));
    // Tamer level 5: 5 % of each stat.
    CHECK(raising.state().stats.base[stat] == baby.stats[stat] + (before[stat] * 5 / 100));
  }
  CHECK(raising.partner().alive());
  CHECK(raising.state().life.age == 0);
  CHECK(raising.state().life.lives == 3);
  CHECK(raising.state().life.remainingLifetime == 360);
  CHECK(raising.state().evolution.careMistakes == 0);
}

TEST_CASE("care.md rebirth: a partner that did not die naturally passes nothing on") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.edit([](PartnerState& state) { state.life.lives = 1; });
  loseBattle(raising);
  REQUIRE(raising.partner().rebirth(raising.clock(), 10).has_value());
  const std::int32_t reborn = testing::valueOf(raising.partner().takeEvents(), EventKind::Reborn);
  const RebirthBaby& baby =
      raising.rules().life.rebirth.babies.at(static_cast<std::size_t>(reborn));
  CHECK(raising.state().stats.base == baby.stats);
}

TEST_CASE("care.md rebirth: a living partner is not reborn") {
  OriginalRuleset original;
  Raising raising(original.rules());
  CHECK(testing::errorOf(raising.partner().rebirth(raising.clock(), 1)) == messages::StillAlive);
}

TEST_CASE("care.md rebirth: the baby is a strategy; another one picks differently") {
  auto registries = std::make_shared<PartnerStrategyRegistries>();
  registries->rebirthChoices.add("first", [] { return std::make_unique<FirstBaby>(); });
  OriginalRuleset original(registries);
  original.set("life", "rebirth.baby_choice", "first");
  for (std::uint64_t seed = 1; seed <= 4; ++seed) {
    Raising raising(original.rules(), "agu", seed);
    raising.quiet();
    dieNaturally(raising);
    REQUIRE(raising.partner().rebirth(raising.clock(), 0).has_value());
    CHECK(raising.state().species == "bota");
  }
}

TEST_CASE("evolution: a partner that becomes another species says so, for the field to draw it") {
  OriginalRuleset original;
  Raising raising(original.rules());
  static_cast<void>(raising.partner().takeEvents());
  REQUIRE(raising.partner().changeSpecies("greymon", raising.clock()));
  CHECK(raising.state().species == "greymon");
  CHECK(has(raising.partner().takeEvents(), EventKind::Evolved));
  // An unknown species changes nothing and says nothing.
  CHECK_FALSE(raising.partner().changeSpecies("nobody", raising.clock()));
  CHECK_FALSE(has(raising.partner().takeEvents(), EventKind::Evolved));
}

} // namespace dwe::game::partner
