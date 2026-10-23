#pragma once

#include <dwe/game/battle/combatant.hpp>

#include <cstdint>

namespace dwe::game::battle {

// The ground a battle is fought on, as the engine sees it: the camera that frames the fight and the
// map under it (map.c: entityCheckCollision). The battle asks it before every step; the fighters
// themselves are tested by the battle. Without one (a simulation, the tests) the fighters stay
// within the ruleset's arena radius of where they started.
class BattleGround {
public:
  BattleGround() = default;
  BattleGround(const BattleGround&) = delete;
  BattleGround& operator=(const BattleGround&) = delete;
  BattleGround(BattleGround&&) = delete;
  BattleGround& operator=(BattleGround&&) = delete;
  virtual ~BattleGround() = default;

  // Whether a fighter of `radius` stepping from `from` to `to` leaves the combat area, the part of
  // the screen the fight stays in (map.c: entityCheckCombatArea, hasMovedOutsideCombatArea).
  [[nodiscard]] virtual bool leavesCombatArea(Point from, Point to, std::int32_t radius) const = 0;
  // Whether the map's tiles stop that step (map.c: checkMapCollision).
  [[nodiscard]] virtual bool blocksStep(Point from, Point to, std::int32_t radius) const = 0;
};

} // namespace dwe::game::battle
