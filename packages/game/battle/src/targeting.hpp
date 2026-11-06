#pragma once

#include <dwe/game/battle/strategies.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dwe::game::battle {

// How good a target `other` is for `self`, lower is better (battle_main.c:
// BTL_calculateTargetScore).
std::int32_t targetScore(const BattleView& view, FighterId self, FighterId other);

// The weakest other fighter by targetScore, leaving out the current target (battle_main.c:
// BTL_getWeakestEnemy). `score` is the one the original reports: the last one it computed, or the
// best one without the quirk.
struct WeakestChoice {
  std::optional<FighterId> fighter;
  std::int32_t score = 0;
};
WeakestChoice weakestOther(const BattleView& view, FighterId self);

// The other living fighters with a status effect (battle_main.c: BTL_getStatusAfflictedEnemies).
std::vector<FighterId> afflictedOthers(const BattleView& view, FighterId self);

// The nearest other living fighter (battle_main.c: BTL_getNearestEnemy).
std::optional<FighterId> nearestOther(const BattleState& state, FighterId self);

// The special a fighter's species is first, for the affinities; nullopt when it has none.
std::optional<std::int32_t> firstSpecial(const Fighter& fighter);

// A weighted roll over `weights` as the original does it: a roll below the total, walked over the
// non-zero weights. The index past the last one when nothing is picked (a total of 0).
std::size_t weightedPick(ruleset::Random& random, std::span<const std::int32_t> weights);

} // namespace dwe::game::battle
