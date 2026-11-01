#include <dwe/game/battle/status_visuals.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

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

TEST_CASE(
    "battle_effect.c BTL_addFinisherAura: two slots, frame 0 to its duration, one roll a frame "
    "for the outer object") {
  StatusVisuals visuals;
  dwe::ruleset::Random random{7};
  REQUIRE(visuals.addFinisherAura(PartnerId, 0x50) == std::optional<std::size_t>{0});
  REQUIRE(visuals.addFinisherAura(1, 0x50) == std::optional<std::size_t>{1});
  CHECK_FALSE(visuals.addFinisherAura(2, 0x50).has_value());
  visuals.removeFinisherAura(1);
  const dwe::ruleset::Random before = random;
  for (int frame = 0; frame < 0x50; ++frame) {
    visuals.tick(random);
  }
  REQUIRE(visuals.finisherAuras()[0].has_value());
  CHECK(visuals.finisherAuras()[0]->frame == 0x50);
  CHECK(random.state() == advanced(before, 0x50).state());
  visuals.tick(random);
  CHECK_FALSE(visuals.finisherAuras()[0].has_value());
  CHECK(random.state() == advanced(before, 0x50).state());
}

TEST_CASE("battle_effect.c BTL_tickItemParticles: 20 glows from over the head that fall and fade "
          "over 25 frames, gone after 40") {
  StatusVisuals visuals;
  Fighter body;
  body.combatant.height = 300;
  body.position = {.x = 100, .z = -50};
  dwe::ruleset::Random random{1};
  visuals.addItemParticles(PartnerId, body);
  const auto& burst = visuals.itemBursts()[0];
  REQUIRE(burst.has_value());
  CHECK(burst->origin == Point{.x = 100, .z = -50});
  const StatusVisuals::ItemParticle start = burst->particles[0];
  CHECK(start.position[1] == -300 * 16);
  // Straight up at 0x230, tilted at most 500 of 4096 a turn.
  CHECK(start.velocity[1] < -0x1C0);
  visuals.tick(random);
  CHECK(burst->particles[0].life == 0x18);
  CHECK(burst->particles[0].position[1] == start.position[1] + start.velocity[1] + 0x36);
  CHECK(burst->particles[0].brightness == 0xFF0 - 163);
  for (int frame = 1; frame <= 0x28; ++frame) {
    visuals.tick(random);
  }
  CHECK_FALSE(visuals.itemBursts()[0].has_value());
}

TEST_CASE("battle_effect.c BTL_tickBuffDisk: the disk grows from a third, spins 0x32 a frame and "
          "ends after 0x43 frames; its trails rise from the feet") {
  StatusVisuals visuals;
  Fighter body;
  body.combatant.radius = 200;
  body.combatant.height = 200;
  body.position = {.x = 0, .z = 0};
  const std::vector<Fighter> fighters{body};
  dwe::ruleset::Random random{1};
  visuals.addBuffDisk(PartnerId, body);
  visuals.tick(random, fighters);
  REQUIRE(visuals.buffDisk().has_value());
  CHECK(visuals.buffDisk()->scaleXZ == 0xBB8 / 3);
  CHECK(visuals.buffDisk()->rotationY == 0x32);
  const auto& trail = visuals.buffTrails()->trails[0];
  CHECK(trail.life == 7);
  CHECK(trail.corners[0][1] == 100);
  CHECK(trail.corners[2][1] == 0x25);
  // 160 to 198 from the feet for a radius of 200, 60 wide.
  const auto reach = std::hypot(static_cast<double>(trail.corners[0][0]),
                                static_cast<double>(trail.corners[0][2]));
  CHECK(reach >= 160.0);
  CHECK(reach <= 202.0);
  for (int frame = 1; frame < 0x43; ++frame) {
    visuals.tick(random, fighters);
  }
  REQUIRE(visuals.buffDisk().has_value());
  CHECK(visuals.buffDisk()->scaleXZ < 0xBB8 * 2 / 10);
  visuals.tick(random, fighters);
  CHECK_FALSE(visuals.buffDisk().has_value());
  CHECK_FALSE(visuals.buffTrails().has_value());
}

TEST_CASE("battle_effect.c BTL_addBuffRingsEffect: the rings scale with the radius and spark in "
          "the cured status's colour") {
  StatusVisuals visuals;
  Fighter body;
  body.combatant.radius = 100;
  dwe::ruleset::Random random{1};
  visuals.addBuffRings(PartnerId, body, StatusVisuals::CureKind::Confusion);
  REQUIRE(visuals.buffRings().has_value());
  CHECK(visuals.buffRings()->target == 4096 - 200);
  CHECK(StatusVisuals::ringColor(visuals.buffRings()->kind) == StatusVisuals::Vec3{235, 150, 220});
  visuals.tick(random);
  CHECK(visuals.buffRings()->scale == (4096 - 200) / 3);
  CHECK(visuals.buffRings()->rotations[1] == std::array<std::int32_t, 2>{0x258 + 12, 0x258 + 20});
  for (int frame = 1; frame <= 0x41; ++frame) {
    visuals.tick(random);
  }
  CHECK_FALSE(visuals.buffRings().has_value());
}

} // namespace battle_test
