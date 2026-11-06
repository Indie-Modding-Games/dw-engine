#include <dwe/game/partner/partner.hpp>
#include <dwe/game/partner/poop_piles.hpp>
#include <dwe/ruleset/random.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>

#include "partner_fixture.hpp"

// docs/reference/care.md, "Baño": one test per rule.
namespace dwe::game::partner {

namespace {

using testing::has;
using testing::OriginalRuleset;
using testing::Raising;

// The test rookie needs the toilet every 18 steps of 10 minutes and holds on (50 + 20) × 1200 / 100
// frames.
constexpr std::int32_t UrgeFrames = 840;

void putOffMeals(Raising& raising) {
  raising.edit([](PartnerState& state) { state.hunger.foodLevel = 1'000'000; });
}

void needToilet(Raising& raising) {
  raising.edit([](PartnerState& state) { state.toilet.poopLevel = 1; });
  raising.playMinutes(10);
  REQUIRE(raising.state().conditions.has(Condition::NeedsToilet));
}

} // namespace

TEST_CASE("care.md toilet: the partner needs the toilet after its species' steps of 10 minutes") {
  OriginalRuleset original;
  Raising raising(original.rules());
  putOffMeals(raising);
  CHECK(raising.state().toilet.poopLevel == 18);
  CHECK_FALSE(has(raising.playMinutes(179), EventKind::NeedsToilet));
  CHECK(has(raising.playMinutes(1), EventKind::NeedsToilet));
  CHECK(raising.state().toilet.urgeFrames == UrgeFrames);
}

TEST_CASE("care.md toilet: a partner that holds on too long goes on the ground: a care mistake") {
  OriginalRuleset original;
  Raising raising(original.rules());
  putOffMeals(raising);
  needToilet(raising);
  const MoodState before = raising.state().mood;
  const std::int32_t urge = raising.state().toilet.urgeFrames;
  CHECK_FALSE(has(raising.playFrames(urge - 1), EventKind::PoopedOnGround));
  const auto events = raising.playFrames(1);
  const std::int32_t pile = testing::valueOf(events, EventKind::PoopedOnGround);
  CHECK(pile == 8);
  CHECK(has(events, EventKind::CareMistake));
  CHECK(raising.state().evolution.careMistakes == 1);
  CHECK(raising.state().mood.happiness == before.happiness - 10);
  CHECK(raising.state().mood.discipline == before.discipline - 5);
  CHECK(raising.state().mood.scoldBoost == ScoldBoost::Ready);
  CHECK(raising.state().toilet.virusBar == 1);
  CHECK(raising.state().toilet.poopLevel == 36);
  CHECK_FALSE(raising.state().conditions.has(Condition::NeedsToilet));
}

TEST_CASE("care.md toilet: with a field the pile and its care effects wait for the pile's scene") {
  OriginalRuleset original;
  Raising raising(original.rules());
  putOffMeals(raising);
  needToilet(raising);
  const MoodState before = raising.state().mood;
  const CareContext field{.fieldScenes = true};
  const std::int32_t urge = raising.state().toilet.urgeFrames;
  const auto due = raising.playFrames(urge, field);
  CHECK_FALSE(has(due, EventKind::PoopedOnGround));
  CHECK_FALSE(has(due, EventKind::CareMistake));
  CHECK(raising.partner().wildPoopDue());
  CHECK(raising.state().conditions.has(Condition::NeedsToilet));
  CHECK(raising.state().mood.scoldBoost == ScoldBoost::Ready);
  CHECK(raising.state().evolution.careMistakes == 0);
  CHECK_FALSE(has(raising.playMinutes(10, field), EventKind::PoopedOnGround));

  raising.partner().leaveWildPile();
  const auto placed = raising.partner().takeEvents();
  CHECK(testing::valueOf(placed, EventKind::PoopedOnGround) == 8);
  CHECK(has(placed, EventKind::CareMistake));
  CHECK_FALSE(raising.partner().wildPoopDue());
  CHECK(raising.state().evolution.careMistakes == 1);
  CHECK(raising.state().mood.happiness == before.happiness - 10);
  CHECK(raising.state().mood.discipline == before.discipline - 5);
  CHECK(raising.state().toilet.poopLevel == 36);
  CHECK_FALSE(raising.state().conditions.has(Condition::NeedsToilet));
  raising.partner().leaveWildPile();
  CHECK(raising.partner().takeEvents().empty());
  CHECK(raising.state().evolution.careMistakes == 1);
}

TEST_CASE(
    "care.md toilet: the urge runs only in the normal state with the player free, and a pile due "
    "keeps the toilet cell from sending the partner") {
  OriginalRuleset original;
  Raising raising(original.rules());
  putOffMeals(raising);
  needToilet(raising);
  CHECK(raising.partner().goesToToilet());
  // partner_impl.c: tickPoopingMechanic runs from partnerTickNormal (state 1) and returns while
  // tamerGetState() != 0: a care scene or a held player does not bring the pile closer, even with
  // the urge already run out (urge -1).
  raising.edit([](PartnerState& state) { state.toilet.urgeFrames = -1; });
  const CareContext careScene{.partnerNormal = false, .fieldScenes = true};
  const CareContext heldPlayer{.tamerIdle = false, .fieldScenes = true};
  CHECK_FALSE(has(raising.playFrames(UrgeFrames, careScene), EventKind::PoopedOnGround));
  CHECK_FALSE(has(raising.playFrames(UrgeFrames, heldPlayer), EventKind::PoopedOnGround));
  CHECK_FALSE(raising.partner().wildPoopDue());
  CHECK(raising.state().toilet.urgeFrames == -1);
  CHECK(raising.partner().goesToToilet());
  // Back in the normal state the run-out urge goes at once: the pile waits for its scene, which
  // holds the player (tickWildPoop: tamerSetState(6)), so the toilet cell no longer sends it.
  raising.playFrames(1, CareContext{.fieldScenes = true});
  CHECK(raising.partner().wildPoopDue());
  CHECK(raising.state().conditions.has(Condition::NeedsToilet));
  CHECK_FALSE(raising.partner().goesToToilet());
  raising.partner().leaveWildPile();
  CHECK_FALSE(raising.partner().goesToToilet());
}

TEST_CASE("care.md toilet: a pile waiting for its scene comes at once once the field is gone") {
  OriginalRuleset original;
  Raising raising(original.rules());
  putOffMeals(raising);
  needToilet(raising);
  raising.playFrames(raising.state().toilet.urgeFrames, CareContext{.fieldScenes = true});
  REQUIRE(raising.partner().wildPoopDue());
  const auto events = raising.playFrames(1);
  CHECK(has(events, EventKind::PoopedOnGround));
  CHECK(has(events, EventKind::CareMistake));
  CHECK_FALSE(raising.partner().wildPoopDue());
}

TEST_CASE(
    "care.md toilet: the toilet gives happiness +2 and discipline +2 and puts off the next need twice the "
    "interval") {
  OriginalRuleset original;
  Raising raising(original.rules());
  putOffMeals(raising);
  needToilet(raising);
  raising.edit([](PartnerState& state) { state.weight = 20; });
  const MoodState before = raising.state().mood;
  raising.partner().useToilet();
  CHECK(has(raising.partner().takeEvents(), EventKind::UsedToilet));
  CHECK(raising.state().mood.happiness == before.happiness + 2);
  CHECK(raising.state().mood.discipline == before.discipline + 2);
  CHECK(raising.state().toilet.poopLevel == 36);
  // (random(4) + 8) / 4.
  CHECK(raising.state().weight == 18);
  CHECK_FALSE(raising.state().conditions.has(Condition::NeedsToilet));
}

TEST_CASE("care.md toilet: without the quirk the toilet waits one interval") {
  OriginalRuleset original;
  original.set("care", "quirks.toilet_doubles_poop_interval", "false");
  Raising raising(original.rules());
  putOffMeals(raising);
  needToilet(raising);
  raising.partner().useToilet();
  CHECK(raising.state().toilet.poopLevel == 18);
}

TEST_CASE("care.md toilet: a partner that does not need it does not use the toilet") {
  OriginalRuleset original;
  Raising raising(original.rules());
  const MoodState before = raising.state().mood;
  raising.partner().useToilet();
  raising.partner().usePortableToilet();
  CHECK(raising.partner().takeEvents().empty());
  CHECK(raising.state().mood == before);
}

TEST_CASE("care.md toilet: the portable toilet waits one interval and leaves the mood alone") {
  OriginalRuleset original;
  Raising raising(original.rules());
  putOffMeals(raising);
  needToilet(raising);
  const MoodState before = raising.state().mood;
  raising.partner().usePortableToilet();
  CHECK(raising.state().toilet.poopLevel == 18);
  CHECK(raising.state().mood.happiness == before.happiness);
  CHECK(raising.state().mood.discipline == before.discipline);
}

TEST_CASE("care.md toilet: each food brings the toilet a step closer") {
  OriginalRuleset original;
  Raising raising(original.rules());
  FoodEffect snack;
  snack.item = "plain";
  snack.meal = false;
  raising.partner().eat(snack, raising.clock());
  CHECK(raising.state().toilet.poopLevel == 17);
}

TEST_CASE("care.md toilet: the sixteenth pile on the ground fills the virus bar") {
  OriginalRuleset original;
  Raising raising(original.rules());
  putOffMeals(raising);
  raising.edit([](PartnerState& state) { state.toilet.virusBar = 15; });
  needToilet(raising);
  // The bar is checked before the toilet in a frame, so it fills on the frame after the pile.
  CHECK_FALSE(has(raising.playFrames(UrgeFrames), EventKind::VirusBarFull));
  CHECK(has(raising.playFrames(1), EventKind::VirusBarFull));
  CHECK(raising.state().toilet.virusBar == 0);
  CHECK(raising.state().virusBackup.stats == raising.state().stats.base);
  CHECK(raising.state().virusBackup.species == raising.state().species);
}

TEST_CASE("care.md toilet: the virus form's piles do not fill the bar") {
  OriginalRuleset original;
  Raising raising(original.rules(), "scum");
  putOffMeals(raising);
  needToilet(raising);
  raising.playFrames(UrgeFrames);
  CHECK(raising.state().toilet.virusBar == 0);
}

TEST_CASE("care.md toilet: the poop eaters heal more the bigger the pile") {
  OriginalRuleset original;
  Raising raising(original.rules(), "nume");
  CHECK(raising.partner().eatsPoop());
  const auto eaten = [&](std::int32_t size) {
    raising.edit([](PartnerState& state) {
      state.stats.currentHp = 0;
      state.stats.currentMp = 0;
    });
    raising.partner().eatPoop(size);
    return raising.state().stats;
  };
  CHECK(eaten(8).currentHp == 1000 / 20);
  CHECK(eaten(8).currentMp == 1000 / 50);
  CHECK(eaten(12).currentHp == 1000 / 10);
  CHECK(eaten(14).currentHp == 1000 / 2);
  Raising other(original.rules());
  CHECK_FALSE(other.partner().eatsPoop());
}

TEST_CASE(
    "care.md toilet: piles go in front of the partner, a random step away when the tile is taken") {
  OriginalRuleset original;
  const PartnerRules rules = original.rules();
  PoopPiles piles(rules.care.toilet);
  ruleset::Random random(1);
  // Facing a quarter turn, the tile in front.
  const Tile first = piles.place("map", {.x = 5, .y = 5}, 0, 8, random);
  CHECK(piles.at("map", first).has_value());
  const Tile second = piles.place("map", {.x = 5, .y = 5}, 0, 8, random);
  CHECK(second != first);
  CHECK(piles.onMap("map").size() == 2);
  CHECK(piles.onMap("other").empty());
  CHECK(piles.remove("map", first) == 8);
  CHECK_FALSE(piles.at("map", first).has_value());
  CHECK_FALSE(piles.remove("map", first).has_value());
}

TEST_CASE("care.md toilet: a map holds 16 piles and the world 100; past that the oldest go") {
  OriginalRuleset original;
  const PartnerRules rules = original.rules();
  PoopPiles piles(rules.care.toilet);
  ruleset::Random random(1);
  for (std::int32_t pile = 0; pile < 20; ++pile) {
    piles.place("map", {.x = pile * 3, .y = 0}, 0, 4, random);
  }
  CHECK(piles.onMap("map").size() == 16);
  for (std::int32_t map = 0; map < 10; ++map) {
    for (std::int32_t pile = 0; pile < 12; ++pile) {
      piles.place(std::format("map{}", map), {.x = pile * 3, .y = 0}, 0, 4, random);
    }
  }
  CHECK(piles.slots().size() == 100);
  std::size_t used = 0;
  for (const PoopPile& slot : piles.slots()) {
    used += slot.size > 0 ? 1 : 0;
  }
  CHECK(used == 100);
  PoopPiles restored(rules.care.toilet);
  restored.restore({piles.slots().begin(), piles.slots().end()}, piles.cursor());
  CHECK(std::ranges::equal(restored.slots(), piles.slots()));
  CHECK(restored.cursor() == piles.cursor());
}

} // namespace dwe::game::partner
