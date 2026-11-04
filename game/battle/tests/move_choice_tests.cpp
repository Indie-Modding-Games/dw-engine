#include <dwe/ruleset/random.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "../src/usability.hpp"
#include "bench.hpp"

namespace battle_test {

// battle_main.c: BTL_getRandomUsableMove, BTL_hasAffordableMoves, BTL_getMoveWithHighestDistance,
// BTL_countLivingEnemies, BTL_resetFlatten, BTL_addBlockedAttack and BTL_clearBlockedAttacks.

TEST_CASE("a random usable move is one roll over the usable slots, in slot order") {
  const std::array<bool, EquippedSlots> usable{false, true, false, true};
  dwe::ruleset::Random random = dwe::ruleset::Random::original(0x1234U);
  dwe::ruleset::Random expected = random;
  const std::int32_t roll = expected.below(2);
  const std::optional<std::size_t> slot = randomUsableSlot(random, usable);
  REQUIRE(slot.has_value());
  CHECK(*slot == (roll == 0 ? 1U : 3U));
  CHECK(random.state() == expected.state());

  // One usable slot still takes its roll, as randomLimit(1) does.
  dwe::ruleset::Random single = dwe::ruleset::Random::original(0x1234U);
  CHECK(randomUsableSlot(single, {false, false, true, false}) == std::optional<std::size_t>{2});
  CHECK(single.state() != dwe::ruleset::Random::original(0x1234U).state());

  // None usable: no slot and no roll (anyUsable, BTL_hasAffordableMoves, is asked first).
  dwe::ruleset::Random none = dwe::ruleset::Random::original(0x1234U);
  CHECK_FALSE(anyUsable({false, false, false, false}));
  CHECK_FALSE(randomUsableSlot(none, {false, false, false, false}).has_value());
  CHECK(none.state() == dwe::ruleset::Random::original(0x1234U).state());
}

TEST_CASE("a confused fighter picks one of its usable moves at random") {
  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100), {"punch"}, {"punch", "shot"}), 7};
  BattleContext context = bench.originalContext();
  bench.fighter(1).target = PartnerId;
  // A seed whose first roll keeps the target (into the air, the moves that need one are out).
  std::uint32_t seed = 1;
  while (dwe::ruleset::Random::original(seed).chance(context.rules->partnerAi.confusedAirPercent)) {
    ++seed;
  }
  bench.random = dwe::ruleset::Random::original(seed);
  dwe::ruleset::Random expected = bench.random;
  static_cast<void>(expected.below(100));
  static_cast<void>(expected.below(1));
  const std::int32_t roll = expected.below(2);
  chooseConfusedMove(context, 1);
  CHECK(bench.random.state() == expected.state());
  REQUIRE(bench.fighter(1).queuedMove.has_value());
  CHECK(*bench.fighter(1).queuedMove == Bench::move(roll == 0 ? "punch" : "shot"));
}

TEST_CASE("the longest reach counts every equipped move, the finisher's too") {
  Bench bench{
      duel(evenStats(1000, 100), evenStats(500, 100), {"punch"}, {"punch", "", "", "big_finish"})};
  BattleContext context = bench.context();
  CHECK(longestReach(context, 1) == 0);
  bench.fighter(1).moves.at(FinisherSlot) = Bench::move("shot");
  CHECK(longestReach(context, 1) == Near);
  bench.fighter(1).moves = {};
  CHECK(longestReach(context, 1) == 0);
}

TEST_CASE("the living enemies leave out those the damage on its way beats") {
  BattleSetup setup = duel(evenStats(1000, 100), evenStats(500, 100));
  setup.enemies.push_back(setup.enemies.front());
  setup.enemies.back().position = {.x = 400, .z = 600};
  Bench bench{std::move(setup)};
  CHECK(bench.state.livingEnemies() == 2);
  bench.fighter(2).pendingDamage = bench.fighter(2).hp;
  CHECK(bench.state.livingEnemies() == 1);
  bench.fighter(2).pendingDamage = bench.fighter(2).hp - 1;
  CHECK(bench.state.livingEnemies() == 2);
  bench.fighter(1).hp = 0;
  CHECK(bench.state.livingEnemies() == 1);
}

TEST_CASE("a fighter that faints flattened is back to its shape") {
  Bench bench{duel(evenStats(1000, 100), evenStats(500, 100))};
  BattleContext context = bench.context();
  Fighter& enemy = bench.fighter(1);
  enemy.status.flattened = true;
  enemy.status.flatFrames = 30;
  enemy.hp = 0;
  faint(context, 1);
  CHECK_FALSE(enemy.status.flattened);
  CHECK(enemy.status.flatFrames == 0);
}

TEST_CASE("a guard notes each attack once and forgets them as it drops") {
  BattleSetup setup = duel(evenStats(1000, 100), evenStats(500, 100), {"punch", "shot"});
  setup.enemies.push_back(setup.enemies.front());
  setup.enemies.back().position = {.x = 400, .z = 600};
  Bench bench{std::move(setup)};
  BattleContext context = bench.context();
  const auto attackOf = [](FighterId attacker, MoveRef move) {
    return AttackInFlight{.attacker = attacker,
                          .defender = PartnerId,
                          .move = move,
                          .origin = {},
                          .framesLeft = 0,
                          .age = std::nullopt,
                          .clip = std::nullopt,
                          .hitPoint = std::nullopt,
                          .touch = std::nullopt,
                          .facingTarget = {}};
  };
  const MoveRef punch = Bench::move("punch");
  const MoveRef shot = Bench::move("shot");
  const MoveRef flat = context.moves->flattenedShot();
  CHECK(registerBlockedAttack(context, attackOf(1, punch)));
  // The same fighter's same move again: no new spark.
  CHECK_FALSE(registerBlockedAttack(context, attackOf(1, punch)));
  CHECK(registerBlockedAttack(context, attackOf(1, shot)));
  CHECK(registerBlockedAttack(context, attackOf(2, punch)));
  // Every flattened fighter's shot is the same effect (0x179).
  CHECK(registerBlockedAttack(context, attackOf(1, flat)));
  CHECK_FALSE(registerBlockedAttack(context, attackOf(2, flat)));
  CHECK(bench.fighter(PartnerId).blockedAttacks.size() == 4);

  // The guard drops once its frames run out (BTL_tickFighterAction: BTL_clearBlockedAttacks).
  Fighter& partner = bench.fighter(PartnerId);
  partner.blocking = true;
  partner.invulnerableFrames = 1;
  endAnimation(context, PartnerId);
  CHECK(partner.blockedAttacks.size() == 4);
  partner.invulnerableFrames = 0;
  endAnimation(context, PartnerId);
  CHECK_FALSE(partner.blocking);
  CHECK(partner.blockedAttacks.empty());
  CHECK(registerBlockedAttack(context, attackOf(1, punch)));
}

} // namespace battle_test
