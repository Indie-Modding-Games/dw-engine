#include <dwe/game/battle/status_visuals.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "bench.hpp"

namespace battle_test {

namespace {

using Kind = StatusVisuals::Kind;

// The generator `draws` rolls further on.
dwe::ruleset::Random advanced(dwe::ruleset::Random random, int draws) {
  for (int draw = 0; draw < draws; ++draw) {
    static_cast<void>(random.next());
  }
  return random;
}

std::size_t countOf(const auto& pool) {
  return static_cast<std::size_t>(
      std::ranges::count_if(pool, [](const auto& slot) { return slot.has_value(); }));
}

} // namespace

TEST_CASE("a confusion's objects take three of the battle's rolls with the original's rolls on") {
  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100))};
  bench.random = dwe::ruleset::Random::original(0x5CF299CBU);
  const dwe::ruleset::Random before = bench.random;
  BattleContext context = bench.originalContext();
  applyMoveStatus(context, 1, Bench::move("dizzy_ray"));
  REQUIRE(bench.fighter(1).status.confused);
  // The status chance, the confusion's frames and BTL_addConfusionEffect's three rand().
  CHECK(bench.random.state() == advanced(before, 5).state());
  CHECK(bench.visuals.shownBy(1) == 0U);
  REQUIRE(bench.visuals.confusions()[0].has_value());
  CHECK(bench.visuals.confusions()[0]->fighter == 1);

  // Without the original's rolls the battle's generator keeps to its own two.
  Bench plain{duel(evenStats(1000, 100), evenStats(500, 100))};
  plain.random = dwe::ruleset::Random::original(0x5CF299CBU);
  BattleContext own = plain.context();
  applyMoveStatus(own, 1, Bench::move("dizzy_ray"));
  CHECK(plain.random.state() == advanced(before, 2).state());
  CHECK(plain.visuals.shownBy(1) == 0U);
}

TEST_CASE("poison shows a bubble every 30 frames for 40, two rolls each") {
  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100))};
  bench.random = dwe::ruleset::Random::original(1U);
  BattleContext context = bench.originalContext();
  applyMoveStatus(context, 1, Bench::move("poison_bite"));
  REQUIRE(bench.visuals.poisons() == 1);
  CHECK(countOf(bench.visuals.bubbles()) == 0);

  const dwe::ruleset::Random before = bench.random;
  bench.visuals.tick(bench.random);
  CHECK(bench.random.state() == advanced(before, 2).state());
  REQUIRE(countOf(bench.visuals.bubbles()) == 1);
  const StatusVisuals::Bubble bubble = *bench.visuals.bubbles()[0];
  CHECK(bubble.fighter == 1);
  CHECK(bubble.frame == 1);
  CHECK(bubble.offsetX >= -50);
  CHECK(bubble.offsetX < 50);
  CHECK(bubble.offsetZ >= -50);
  CHECK(bubble.offsetZ < 50);

  for (int frame = 2; frame <= 30; ++frame) {
    bench.visuals.tick(bench.random);
  }
  CHECK(bench.random.state() == advanced(before, 2).state());
  bench.visuals.tick(bench.random); // the 31st: the next bubble
  CHECK(bench.random.state() == advanced(before, 4).state());
  CHECK(countOf(bench.visuals.bubbles()) == 2);
  for (int frame = 32; frame <= 40; ++frame) {
    bench.visuals.tick(bench.random);
  }
  CHECK(countOf(bench.visuals.bubbles()) == 1); // the first ended on its 40th frame
}

TEST_CASE("the stun's sign shows over the others and leaves the confusion when it ends") {
  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100))};
  BattleContext context = bench.context();
  applyMoveStatus(context, 1, Bench::move("poison_bite"));
  applyMoveStatus(context, 1, Bench::move("dizzy_ray"));
  CHECK(bench.visuals.poisons() == 0);
  REQUIRE(countOf(bench.visuals.confusions()) == 1);

  applyMoveStatus(context, 1, Bench::move("stun_slap"));
  CHECK(countOf(bench.visuals.confusions()) == 0);
  REQUIRE(bench.visuals.stuns()[0].has_value());
  const std::int32_t stun = bench.fighter(1).status.stunFrames;
  CHECK(bench.visuals.stuns()[0]->duration == stun);
  bench.fighter(1).status.confusionFrames = 10 * stun; // the confusion outlasts the stun

  // A ring every 6 frames, 16 frames each.
  for (int frame = 1; frame <= 12; ++frame) {
    bench.visuals.tick(bench.random);
  }
  CHECK(countOf(bench.visuals.stunRings()) == 2);

  // BTL_clearStun puts the confusion back (the poison waits under it).
  for (int frame = 0; frame < stun; ++frame) {
    tickStatusEffects(context);
  }
  REQUIRE_FALSE(bench.fighter(1).status.stunned);
  CHECK_FALSE(bench.visuals.stuns()[0].has_value());
  CHECK(countOf(bench.visuals.confusions()) == 1);
  CHECK(bench.visuals.poisons() == 0);

  // The cure ends both: the poison first, under the confusion, then the confusion.
  cureStatus(context, 1);
  CHECK(countOf(bench.visuals.confusions()) == 0);
  CHECK(bench.visuals.poisons() == 0);
  CHECK_FALSE(bench.visuals.shownBy(1).has_value());
}

TEST_CASE("a stun sign that runs out stays the fighter's until the stun ends") {
  StatusVisuals visuals;
  dwe::ruleset::Random random{1};
  visuals.show(random, 2, Kind::Stun, 10);
  // 10 frames counting, 7 more flashing and 4 shrinking.
  for (int frame = 1; frame < 21; ++frame) {
    visuals.tick(random);
  }
  REQUIRE(visuals.stuns()[0].has_value());
  CHECK(visuals.stuns()[0]->stage == StatusVisuals::StunStage::Shrink);
  visuals.tick(random);
  CHECK_FALSE(visuals.stuns()[0].has_value());
  CHECK(visuals.shownBy(2) == 0U);
  // Nothing else shows meanwhile (statusFxId is still set).
  visuals.show(random, 2, Kind::Confusion);
  CHECK(countOf(visuals.confusions()) == 0);
  visuals.hide(2, Kind::Stun);
  CHECK_FALSE(visuals.shownBy(2).has_value());
}

TEST_CASE("fainting and the flattening end the fighter's status visual") {
  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100))};
  BattleContext context = bench.context();
  applyMoveStatus(context, 1, Bench::move("poison_bite"));
  REQUIRE(bench.visuals.poisons() == 1);
  applyMoveStatus(context, 1, Bench::move("squash"));
  CHECK(bench.visuals.poisons() == 0);
  CHECK_FALSE(bench.visuals.shownBy(1).has_value());

  applyMoveStatus(context, PartnerId, Bench::move("dizzy_ray"));
  REQUIRE(countOf(bench.visuals.confusions()) == 1);
  faint(context, PartnerId);
  CHECK(countOf(bench.visuals.confusions()) == 0);
}

} // namespace battle_test
