#include <dwe/game/field/player_trail.hpp>

#include <algorithm>
#include <cstdlib>

namespace dwe::game::field {

namespace {

constexpr std::int32_t GridLast = assets::MapCollision::Size - 1;

assets::CollisionCell clampToGrid(assets::CollisionCell cell) {
  return {.column = std::clamp(cell.column, 0, GridLast), .row = std::clamp(cell.row, 0, GridLast)};
}

} // namespace

bool isCellLineBlocked(const assets::MapCollision& collision, assets::CollisionCell from,
                       assets::CollisionCell to) {
  from = clampToGrid(from);
  to = clampToGrid(to);
  if (from == to) {
    return false;
  }
  const std::int32_t dx = to.column - from.column;
  const std::int32_t dy = to.row - from.row;
  // The longer axis steps one cell at a time; the other follows by the remainder of the division,
  // as the original's integer stepping does.
  const bool alongX = std::abs(dx) >= std::abs(dy);
  const std::int32_t along = alongX ? dx : dy;
  const std::int32_t across = alongX ? dy : dx;
  const std::int32_t distance = std::abs(along);
  const std::int32_t quotient = across / distance;
  const std::int32_t remainder = std::abs(across % distance);
  const std::int32_t sign = across % distance < 0 ? -1 : 1;
  std::int32_t accumulator = distance;
  std::int32_t mainAxis = alongX ? from.column : from.row;
  std::int32_t sideAxis = alongX ? from.row : from.column;
  for (std::int32_t step = 0; step < distance; ++step) {
    if (remainder != 0) {
      accumulator -= remainder;
      if (accumulator <= 0) {
        sideAxis += sign;
        accumulator += distance;
      }
    } else {
      sideAxis += quotient;
    }
    mainAxis += along > 0 ? 1 : -1;
    const assets::CollisionCell cell =
        alongX ? assets::CollisionCell{.column = mainAxis, .row = sideAxis}
               : assets::CollisionCell{.column = sideAxis, .row = mainAxis};
    if (collision.isBlocked(cell)) {
      return true;
    }
  }
  return false;
}

// map.c: initializeTamerWaypoints.
void PlayerTrail::enter(assets::CollisionCell cell) {
  current_ = 0;
  count_ = 0;
  start_ = cell;
  previous_ = cell;
  active_ = 0;
}

// map.c: clearTamerWaypoints.
void PlayerTrail::clear() {
  current_ = 0;
  count_ = 0;
  active_ = 0;
}

// map.c: addTamerWaypoint.
void PlayerTrail::add(std::int32_t index, assets::CollisionCell cell) {
  cells_.at(static_cast<std::size_t>(index % Capacity)) = cell;
  ++count_;
}

// map.c: tickTamerWaypoints: on a new cell, the cell left is kept when the line back to the start
// is blocked and the newest kept cell is out of sight too; a clear line back forgets them all. With
// two or more kept, the active one walks back from the newest, and once it is in sight the newer
// ones are dropped.
void PlayerTrail::tick(assets::CollisionCell cell, const assets::MapCollision& collision) {
  if (cell != previous_) {
    if (isCellLineBlocked(collision, cell, start_)) {
      if (count_ != 0) {
        const std::int32_t newest = (current_ + count_ - 1) % Capacity;
        if (isCellLineBlocked(collision, cell, cells_.at(static_cast<std::size_t>(newest)))) {
          add((current_ + count_) % Capacity, previous_);
        }
      } else {
        add(0, previous_);
      }
    } else {
      clear();
    }
  }
  if (count_ > 1) {
    const auto active = static_cast<std::size_t>(std::clamp(active_, 0, Capacity - 1));
    if (!isCellLineBlocked(collision, cell, cells_.at(active))) {
      count_ = active_ + 1;
      active_ = count_ > 1 ? count_ - 2 : count_ - 1;
    } else {
      --active_;
      if (active_ < 0) {
        active_ = count_ - 2;
      }
    }
  }
  previous_ = cell;
}

void PlayerTrail::keepOldest(std::int32_t count) {
  if (count <= 0) {
    clear();
    return;
  }
  count_ = std::min(count, count_);
  active_ = count_ - 1;
}

// battle_setup.c: BTL_handleBattleIntro reads TAMER_WAYPOINT_X/Y from COUNT - 1 down to 0. The
// original never moves TAMER_WAYPOINT_CURRENT off 0 and reads past the ring after 30 cells; this
// stops at its end.
std::vector<assets::CollisionCell> PlayerTrail::newestFirst() const {
  std::vector<assets::CollisionCell> cells;
  for (std::int32_t index = std::min(count_, Capacity) - 1; index >= 0; --index) {
    cells.push_back(cells_.at(static_cast<std::size_t>(index)));
  }
  return cells;
}

} // namespace dwe::game::field
