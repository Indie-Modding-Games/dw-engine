#pragma once

// A battle's state taken out of a Battle, with a context over it, to test one rule of the loop at a
// time.

#include <cstdint>
#include <utility>

#include "../src/battle_context.hpp"
#include "fixture.hpp"

namespace battle_test {

struct Bench {
  explicit Bench(BattleSetup setup, std::uint64_t seed = 1,
                 const BattleRules& battleRules = originalRules())
      : rules{&battleRules}, condition{setup.condition}, random{seed} {
    Battle battle = startBattle(std::move(setup), seed, battleRules);
    state = battle.state();
  }

  BattleContext context() {
    return {.rules = rules,
            .moves = &testCatalog(),
            .state = &state,
            .strategies = &strategies,
            .random = &random,
            .events = &events,
            .condition = &condition};
  }
  Fighter& fighter(FighterId id) { return state.fighters.at(id); }
  [[nodiscard]] static MoveRef move(std::string_view id) { return refOf(testCatalog(), id); }

  const BattleRules* rules;
  BattleState state;
  BattleStrategies strategies = originalStrategies();
  PartnerCondition condition;
  dwe::ruleset::Random random;
  BattleEvents events;
};

} // namespace battle_test
