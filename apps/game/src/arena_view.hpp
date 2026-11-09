#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/core/original_math.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/tournament/match_show.hpp>
#include <dwe/game/tournament/tournament.hpp>

#include <glm/ext/vector_int3.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace dwe::app {

// The arena of a tournament's match (story_tournament.hpp) and of a versus battle
// (versus_arena.hpp): where its points are in the battle and in the world, and what its cameras
// read of the fighters.

// The arena's camera centres on its own background, wide enough that no bars show at any window
// size (the original draws the arena over its whole 320 x 240 screen).
inline constexpr float ArenaBackgroundScale = 100.0F;

// The original's z points into the screen, the world's out of it (core::fromOriginalAxes).
[[nodiscard]] inline game::battle::Point battlePointOf(game::tournament::ArenaPoint point) {
  return {.x = point.x, .z = -point.z};
}

[[nodiscard]] inline game::tournament::ArenaPoint arenaPointOf(game::battle::Point point) {
  return {.x = point.x, .z = -point.z};
}

[[nodiscard]] inline glm::vec3 worldOf(const std::array<std::int32_t, 3>& original) {
  return assets::toWorldPosition(glm::ivec3{original[0], original[1], original[2]});
}

// The partner and the rival as the match camera follows them (STD_tickBattle): where each stands,
// its yaw and the range of the technique it performs, if any.
[[nodiscard]] inline std::array<game::tournament::FightFigure, 2>
fightFiguresOf(const game::battle::Battle& fight) {
  std::array<game::tournament::FightFigure, 2> figures{};
  const game::battle::BattleState& state = fight.state();
  const game::battle::MoveCatalog& moves = fight.moves();
  for (std::size_t i = 0; i < figures.size() && i < state.fighters.size(); ++i) {
    const game::battle::Fighter& fighter = state.fighters.at(i);
    game::tournament::FightFigure& figure = figures.at(i);
    figure.at = arenaPointOf(fighter.position);
    // The facing is (sin, -cos) of the original's yaw in world axes.
    figure.yaw = core::original::atan4096(fighter.facing.x, -fighter.facing.z) & 0xfff;
    if (fighter.attack) {
      figure.moveRange = static_cast<std::int32_t>(moves.at(fighter.attack->move).range);
    }
  }
  return figures;
}

} // namespace dwe::app
