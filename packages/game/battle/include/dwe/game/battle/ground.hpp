#pragma once

#include <dwe/game/battle/combatant.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace dwe::game::battle {

// A point of the original's 320 x 240 screen, in pixels from its centre, y down.
struct ScreenPoint {
  std::int32_t x = 0;
  std::int32_t y = 0;
};

// The ground a battle is fought on, as the engine sees it: the camera that frames the fight and the
// map under it (map.c: entityCheckCollision). The battle asks it before every step and tests the
// combat area and the other fighters itself. Without one (a simulation, the tests) the fighters
// stay within the ruleset's arena radius of where they started.
class BattleGround {
public:
  BattleGround() = default;
  BattleGround(const BattleGround&) = delete;
  BattleGround& operator=(const BattleGround&) = delete;
  BattleGround(BattleGround&&) = delete;
  BattleGround& operator=(BattleGround&&) = delete;
  virtual ~BattleGround() = default;

  // Where a point `height` world units over the ground appears on the original's screen, as the
  // camera framing the fight projects it (map.c: entityCheckCombatArea's GTE projection); nothing
  // when it is behind the camera.
  [[nodiscard]] virtual std::optional<ScreenPoint> screenPointOf(Point ground,
                                                                 std::int32_t height) const = 0;
  // Whether the map's tiles stop a step of a fighter of `radius` (map.c: checkMapCollision).
  [[nodiscard]] virtual bool blocksStep(Point from, Point to, std::int32_t radius) const = 0;
  // Where node `node` of a fighter's model (0 the partner, k the k-th enemy) shows on the
  // original's screen as its pose leaves it (the GTE projection of posData[node]); nothing when the
  // ground does not draw the fighter.
  [[nodiscard]] virtual std::optional<ScreenPoint>
  fighterNodeOnScreen(std::size_t /*fighter*/, std::uint32_t /*node*/) const {
    return std::nullopt;
  }
};

} // namespace dwe::game::battle
