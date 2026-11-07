#include <dwe/game/field/boss_arrival.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <optional>

using dwe::game::field::BossArrival;
using dwe::game::field::BossArrivalCues;
using dwe::game::field::BossRing;
using dwe::game::field::BossRingKind;
using dwe::game::field::bossRingScale;
using dwe::game::field::effectLerp;
using dwe::game::field::sin512;

namespace {

std::size_t liveRings(const BossArrival& arrival) {
  std::size_t live = 0;
  for (const auto& ring : arrival.rings()) {
    live += ring.has_value() ? 1U : 0U;
  }
  return live;
}

// Runs frames until the arrival's frame is `target`, returning the cues of that frame.
BossArrivalCues runTo(BossArrival& arrival, std::int32_t target) {
  BossArrivalCues cues;
  while (arrival.arrivalFrame().value_or(-1) < target) {
    cues = arrival.frame({});
  }
  return cues;
}

} // namespace

TEST_CASE("the original's lerp and sine land on its numbers") {
  CHECK(effectLerp(0x10b8, 0x614, 1, 0x12, 1) == 0x10b8);
  CHECK(effectLerp(0x10b8, 0x614, 1, 0x12, 0x12) == 0x614);
  CHECK(effectLerp(0xff, 0, 0x32, 0x52, 0x42) == 0x80);
  // Past t1 it wraps instead of stopping.
  CHECK(effectLerp(0, 0x80, 0x32, 0x122, 0x13f) == 14);
  CHECK(sin512(0) == 0);
  CHECK(sin512(0x80) == 4096);
  CHECK(sin512(0x100) == 0);
  CHECK(sin512(0x180) == -4096);
}

TEST_CASE("routine 40 hides the boss, hums and adds a charge ring every 6 frames") {
  BossArrival arrival;
  const BossArrivalCues cues = arrival.startBuildup();
  CHECK(cues.heldSound == 0);
  CHECK(arrival.bossHidden());
  CHECK(arrival.charging());
  for (int frame = 1; frame < 6; ++frame) {
    arrival.frame({});
  }
  CHECK(liveRings(arrival) == 0);
  arrival.frame({});
  REQUIRE(arrival.rings()[0].has_value());
  CHECK(arrival.rings()[0]->kind == BossRingKind::Buildup);
  // It ticks in the frame it was added.
  CHECK(arrival.rings()[0]->timer == 1);
  // A ring lives 18 frames: from then on three at a time.
  for (int frame = 7; frame <= 40; ++frame) {
    arrival.frame({});
  }
  CHECK(liveRings(arrival) == 3);
  // The charge alone never ends.
  for (int frame = 0; frame < 500; ++frame) {
    arrival.frame({});
  }
  CHECK(arrival.charging());
  CHECK(arrival.bossHidden());
  CHECK_FALSE(arrival.othersHidden());
}

TEST_CASE("a charge ring narrows and rises over its 18 frames") {
  const auto first = bossRingScale({.kind = BossRingKind::Buildup, .timer = 1});
  CHECK(first.across == 0x10b8);
  CHECK(first.high == 0x15c);
  const auto last = bossRingScale({.kind = BossRingKind::Buildup, .timer = 0x12});
  CHECK(last.across == 0x614);
  CHECK(last.high == 0xc3c + 0x15c);
  const auto spawn = bossRingScale({.kind = BossRingKind::Spawn, .timer = 1});
  CHECK(spawn.across == 0x16cc);
  CHECK(spawn.high == 0xae);
}

TEST_CASE("the arrival blacks out at 45, shows the boss alone at 50 and ends at 340") {
  BossArrival arrival;
  arrival.startBuildup();
  for (int frame = 0; frame < 30; ++frame) {
    arrival.frame({});
  }
  CHECK(arrival.startArrival().sound == 1);
  runTo(arrival, 44);
  CHECK_FALSE(arrival.blackout().has_value());
  CHECK(arrival.bossHidden());
  runTo(arrival, 45);
  CHECK(arrival.blackout() == 255);
  CHECK_FALSE(arrival.orbit().has_value());

  const BossArrivalCues rise = runTo(arrival, 50);
  CHECK(rise.animation == 0x21);
  CHECK(rise.stopSounds);
  CHECK(rise.heldSound == 2);
  // eab.c: EAB_tickSpawn gives the boss vabId 5 as it rises.
  CHECK(rise.bossSoundSlot == 5);
  CHECK_FALSE(arrival.bossHidden());
  CHECK(arrival.othersHidden());
  CHECK_FALSE(arrival.charging());
  CHECK(arrival.blackout() == 255);
  CHECK(arrival.solidShares() == 0);
  REQUIRE(arrival.orbit().has_value());
  CHECK(arrival.orbit()->angle == 0x71c);
  CHECK(arrival.orbit()->distance == 0xdac);
  CHECK(arrival.orbit()->height == 0x258);
  // Frame 50 is 14 of 18: no particle yet.
  CHECK(arrival.particles().empty());

  CHECK_FALSE(runTo(arrival, 82).bossSoundSlot.has_value());
  CHECK(arrival.blackout() == 0);
  runTo(arrival, 83);
  CHECK_FALSE(arrival.blackout().has_value());
  CHECK(runTo(arrival, 210).animation == 0x33);
  runTo(arrival, 290);
  CHECK(arrival.solidShares() == 16);
  CHECK(arrival.orbit()->angle == 0x171c);
  runTo(arrival, 319);
  CHECK_FALSE(arrival.orbit().has_value());

  const BossArrivalCues land = runTo(arrival, 320);
  CHECK(land.stopSounds);
  CHECK(land.sound == 3);
  CHECK_FALSE(arrival.othersHidden());
  CHECK(arrival.particles().empty());
  CHECK_FALSE(arrival.orbit().has_value());
  // The last ring is added on frame 329 and gone 9 frames later; the arrival ends on 340.
  runTo(arrival, 329);
  CHECK(liveRings(arrival) > 0);
  runTo(arrival, 339);
  CHECK(liveRings(arrival) == 0);
  CHECK_FALSE(arrival.arrivalOver());
  arrival.frame({});
  CHECK(arrival.arrivalOver());
  CHECK_FALSE(arrival.arrivalFrame().has_value());
  CHECK(liveRings(arrival) == 0);
}

TEST_CASE("the arrival's particles keep the boss's place and fade over 86 frames") {
  BossArrival arrival;
  arrival.startArrival();
  runTo(arrival, 53);
  CHECK(arrival.particles().empty());
  // Frame 54 is 0 of 18, strength 1: green 50 times the sine of the ramp's second step.
  arrival.frame({1.0F, 2.0F, 3.0F});
  REQUIRE(arrival.particles().size() == 1);
  const auto& first = arrival.particles().front();
  CHECK(first.point == glm::vec3{1.0F, 2.0F, 3.0F});
  CHECK(first.timer == 1);
  CHECK(first.rgb[0] == 0x0a);
  CHECK(first.rgb[1] == (50 * sin512(2)) / 4096);
  CHECK(first.rgb[2] == 0x0a);
  // Frames 55 to 62 add one each; 63 to 71 none.
  runTo(arrival, 71);
  CHECK(arrival.particles().size() == 9);
  // The first is drawn 86 frames and goes on the next.
  runTo(arrival, 53 + 0x56);
  CHECK(arrival.particles().front().timer == 0x56);
  CHECK(arrival.particles().front().point == glm::vec3{1.0F, 2.0F, 3.0F});
  arrival.frame({});
  CHECK(arrival.particles().front().timer == 0x56);
  CHECK(arrival.particles().front().point == glm::vec3{0.0F});
  CHECK(dwe::game::field::bossParticleHalfSide({.timer = 0}) == 8);
  CHECK(dwe::game::field::bossParticleHalfSide({.timer = 0x56}) == 0x9c4);
}

TEST_CASE("eab.c EAB_tickSpawn: the roar starts again when it reaches its last frame") {
  BossArrival arrival;
  static_cast<void>(arrival.startBuildup());
  static_cast<void>(arrival.startArrival());
  // Before the rise a finished roar starts again too (the check comes first on every frame).
  CHECK(arrival.frame({}, 0x33).animation == 0x33);
  // Another finished animation is left alone.
  CHECK_FALSE(arrival.frame({}, 0x21).animation.has_value());
  CHECK_FALSE(arrival.frame({}).animation.has_value());
  // Frame 50: the rise's start wins over the roar's.
  runTo(arrival, 49);
  CHECK(arrival.frame({}, 0x33).animation == 0x21);
  // Frame 210 starts the roar; after it, each finish starts it again until the arrival ends.
  CHECK(runTo(arrival, 210).animation == 0x33);
  CHECK(arrival.frame({}, 0x33).animation == 0x33);
  runTo(arrival, 339);
  CHECK(arrival.frame({}, 0x33).animation == 0x33);
  CHECK_FALSE(arrival.arriving());
  CHECK_FALSE(arrival.frame({}, 0x33).animation.has_value());
}

TEST_CASE("eab.c EAB_tickSpawn: the boss turns from wireframe to solid over frames 50 to 290") {
  BossArrival arrival;
  static_cast<void>(arrival.startBuildup());
  static_cast<void>(arrival.startArrival());
  runTo(arrival, 49);
  CHECK(arrival.solidShares() == BossArrival::SolidShares);
  runTo(arrival, 50);
  CHECK(arrival.solidShares() == 0);
  runTo(arrival, 170);
  CHECK(arrival.solidShares() > 0);
  CHECK(arrival.solidShares() < BossArrival::SolidShares);
  runTo(arrival, 290);
  CHECK(arrival.solidShares() == BossArrival::SolidShares);
}

TEST_CASE("eab.c EAB_tick: nine dark flashes start with the arrival and last 50 frames") {
  BossArrival arrival;
  CHECK(arrival.flashes().empty());
  arrival.startArrival();
  REQUIRE(arrival.flashes().size() == 9);
  const auto& first = arrival.flashes().front();
  CHECK(first.point == glm::ivec3{0, -300, -900});
  CHECK(first.scaleTo == 5120);
  CHECK(first.progress == 0);
  // tickEFEFlash counts each frame and takes them away at 0x32.
  runTo(arrival, 49);
  REQUIRE(arrival.flashes().size() == 9);
  CHECK(arrival.flashes().back().progress == 49);
  CHECK(arrival.flashes().back().point == glm::ivec3{-1200, -300, -2500});
  arrival.frame({});
  CHECK(arrival.flashes().empty());
  // A map change takes them with the rest.
  arrival.startArrival();
  arrival.clear();
  CHECK(arrival.flashes().empty());
}
