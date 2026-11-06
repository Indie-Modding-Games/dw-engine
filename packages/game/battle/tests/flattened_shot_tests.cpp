#include <dwe/core/original_math.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

#include "bench.hpp"
#include "fixture.hpp"

using namespace battle_test;

namespace {

// The shot of the original measured (11_battle_agumon, the enemy flattened by hand): an Agumon
// (radius 120, height 250) at (-274, 3257) facing rotation.vy 2923 fires at the partner, another
// Agumon at (421, 3418). The battle's ground is the console's x and its z the other way.
constexpr std::int32_t AgumonRadius = 120;
constexpr std::int32_t AgumonHeight = 250;
constexpr std::int32_t MeasuredYaw = 2923;

// The ground direction of a fighter turned rotation.vy `yaw`: the console's (0, 0, -1) through
// RotMatrix, z the other way.
Facing facingOfYaw(std::int32_t yaw) {
  return {.x = -dwe::core::original::rsin(yaw), .z = dwe::core::original::rcos(yaw)};
}

Bench measuredBench() {
  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100))};
  for (Fighter& fighter : bench.state.fighters) {
    fighter.combatant.radius = AgumonRadius;
    fighter.combatant.height = AgumonHeight;
  }
  bench.fighter(PartnerId).position = {.x = 421, .z = -3418};
  Fighter& enemy = bench.fighter(1);
  enemy.position = {.x = -274, .z = -3257};
  enemy.facing = facingOfYaw(MeasuredYaw);
  return bench;
}

MoveRef shotMove() {
  return testCatalog().flattenedShot();
}

} // namespace

TEST_CASE("the flattened fighter's shot leaves its chest and lands where the original's did") {
  Bench bench = measuredBench();
  BattleContext context = bench.context();
  fireFlattenedShot(context, 1, shotMove());
  REQUIRE(bench.state.flattenedShots[0].has_value());
  const FlattenedShot& shot = *bench.state.flattenedShots[0];
  // BTL_addAuraProjectile: (-153, -125, 3285) and a step of (34, 0, 7) in the console's axes.
  CHECK(shot.at == Point{.x = -153, .z = -3285});
  CHECK(shot.height == 125);
  CHECK(shot.step == Point{.x = 34, .z = -7});

  // BTL_tickAuraProjectile: the box touches the partner on the shot's 14th frame, at (323, 3383).
  for (int frame = 1; frame < 14; ++frame) {
    tickFlattenedShots(context);
    CHECK(bench.fighter(PartnerId).incoming.empty());
  }
  tickFlattenedShots(context);
  CHECK_FALSE(bench.state.flattenedShots[0].has_value());
  REQUIRE(bench.fighter(PartnerId).incoming.size() == 1);
  const AttackInFlight& hit = bench.fighter(PartnerId).incoming.front();
  CHECK(hit.attacker == 1);
  CHECK(hit.move == shotMove());
  CHECK(hit.hitPoint == std::optional<Point>{Point{.x = 323, .z = -3383}});
}

TEST_CASE("a flattened shot that touches no one fades after its 58 frames") {
  Bench bench = measuredBench();
  bench.fighter(1).facing = facingOfYaw(MeasuredYaw + 2048);
  BattleContext context = bench.context();
  fireFlattenedShot(context, 1, shotMove());
  for (int frame = 1; frame < 58; ++frame) {
    tickFlattenedShots(context);
  }
  REQUIRE(bench.state.flattenedShots[0].has_value());
  CHECK(bench.state.flattenedShots[0]->frame == 57);
  tickFlattenedShots(context);
  CHECK_FALSE(bench.state.flattenedShots[0].has_value());
  CHECK(bench.fighter(PartnerId).incoming.empty());
}

TEST_CASE("a flattened shot flies on past a fighter in its hit reaction") {
  Bench bench = measuredBench();
  bench.fighter(PartnerId).struck = true;
  BattleContext context = bench.context();
  fireFlattenedShot(context, 1, shotMove());
  for (int frame = 1; frame <= 14; ++frame) {
    tickFlattenedShots(context);
  }
  CHECK(bench.state.flattenedShots[0].has_value());
  CHECK(bench.fighter(PartnerId).incoming.empty());
  // Once the reaction ends, the next frame inside the box lands.
  bench.fighter(PartnerId).struck = false;
  tickFlattenedShots(context);
  CHECK(bench.fighter(PartnerId).incoming.size() == 1);
}

TEST_CASE("eight flattened shots fly at once and a ninth is not fired") {
  Bench bench = measuredBench();
  BattleContext context = bench.context();
  for (std::size_t i = 0; i <= FlattenedShotSlots; ++i) {
    fireFlattenedShot(context, 1, shotMove());
  }
  for (const std::optional<FlattenedShot>& shot : bench.state.flattenedShots) {
    CHECK(shot.has_value());
  }
}

TEST_CASE("the flattened fighter's attack fires the shot instead of reaching its target") {
  Bench bench = measuredBench();
  Fighter& enemy = bench.fighter(1);
  enemy.attack = ActiveAttack{.move = shotMove(),
                              .target = PartnerId,
                              .frame = 2,
                              .length = 30,
                              .releaseFrame = 2,
                              .released = true,
                              .followed = false};
  BattleContext context = bench.context();
  releaseAttack(context, 1);
  CHECK(bench.state.inFlight.empty());
  REQUIRE(bench.state.flattenedShots[0].has_value());
  CHECK(bench.state.flattenedShots[0]->attacker == 1);
}
