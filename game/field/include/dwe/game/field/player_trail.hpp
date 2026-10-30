#pragma once

#include <dwe/assets/map_document.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace dwe::game::field {

// The way back the player keeps on each map (map.c: initializeTamerWaypoints, tickTamerWaypoints):
// the cell it entered the map on and, while the straight line back to it is blocked, the cells
// where it turned out of sight of the previous one. A battle's opening walks the player back along
// it (battle_setup.c: BTL_handleBattleIntro) and so does a call to flee (battle_tick.c:
// handleFleeing).
class PlayerTrail {
public:
  // The original keeps up to 30 cells in a ring (TAMER_WAYPOINT_X/Y).
  static constexpr std::int32_t Capacity = 30;

  // Entering a map on `cell`: no cells kept, and that one is where the way back ends.
  void enter(assets::CollisionCell cell);
  // A tick of free walking with the player on `cell`; `collision` is the map's grid.
  void tick(assets::CollisionCell cell, const assets::MapCollision& collision);

  // After a battle's opening (battle_setup.c: BTL_handleBattleIntro): the `count` oldest cells
  // the run back did not reach stay, or none.
  void keepOldest(std::int32_t count);

  // The cell the player entered the map on (TAMER_START_TILE).
  [[nodiscard]] assets::CollisionCell start() const { return start_; }
  // The kept cells from the newest to the oldest, the order the way back takes them.
  [[nodiscard]] std::vector<assets::CollisionCell> newestFirst() const;

private:
  void add(std::int32_t index, assets::CollisionCell cell);
  void clear();

  std::array<assets::CollisionCell, Capacity> cells_{};
  std::int32_t current_ = 0;
  std::int32_t count_ = 0;
  std::int32_t active_ = 0;
  assets::CollisionCell start_{};
  assets::CollisionCell previous_{};
};

// Whether the straight line of cells from one cell to another crosses a blocked one (map.c:
// isLinearPathBlocked): cells are clamped to the grid, the start cell is not tested and the end
// cell is.
bool isCellLineBlocked(const assets::MapCollision& collision, assets::CollisionCell from,
                       assets::CollisionCell to);

} // namespace dwe::game::field
