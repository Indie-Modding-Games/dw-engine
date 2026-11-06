#include <dwe/game/field/grid.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

namespace dwe::game::field {

namespace {

using assets::CollisionCell;
using assets::CollisionCellSize;
using assets::MapCollision;

constexpr std::int32_t GridOrigin = MapCollision::Size / 2;
// How far before a blocked cell a move stops: enough to stay in the free cell after float rounding
// at any position of the grid (|x| <= 5000).
constexpr float StopGap = 0.01F;
// Sight lines are sampled at this step, a quarter of a cell.
constexpr float SightStep = CollisionCellSize / 4.0F;
constexpr int SightLines = 5;

// map_collision.c: getModelTile takes the column x / 100 + 50 and the row 50 - z / 100 with C's
// division (towards zero), then takes one off the column for x < 0 and off the row for z > 0. That
// is floor except on exact cell edges: x on a negative multiple of 100 lands one column further
// left, and z at 0 or on a negative multiple one row further. checkMapCollisionX/Z take the row as
// 49 - z / 100, plus one for z < 0, which differs from getModelTile at z = 0. floor puts every edge
// on the same side, for both.
std::int32_t cellIndex(float coordinate) {
  return static_cast<std::int32_t>(std::floor(coordinate / CollisionCellSize)) + GridOrigin;
}

float cellStart(std::int32_t index) {
  return static_cast<float>(index - GridOrigin) * CollisionCellSize;
}

enum class Axis : std::uint8_t { X, Z };

CollisionCell cellOn(Axis axis, std::int32_t along, std::int32_t across) {
  return axis == Axis::X ? CollisionCell{.column = along, .row = across}
                         : CollisionCell{.column = across, .row = along};
}

// Whether any cell of the line `along` is blocked across [from, to] (cell indices, inclusive).
bool lineBlocked(const MapCollision& collision, Axis axis, std::int32_t along, std::int32_t from,
                 std::int32_t to) {
  for (std::int32_t across = from; across <= to; ++across) {
    if (collision.isBlocked(cellOn(axis, along, across))) {
      return true;
    }
  }
  return false;
}

// Moves one coordinate; returns whether a blocked cell stopped it. The probe is the original's
// (map_collision.c: checkMapCollisionX/Z): the line of cells `radius` ahead, `radius / 2` to each
// side.
bool moveAxis(const MapCollision& collision, glm::vec3& position, Axis axis, float distance,
              float radius) {
  if (distance == 0.0F) {
    return false;
  }
  float& along = axis == Axis::X ? position.x : position.z;
  const float across = axis == Axis::X ? position.z : position.x;
  const float sign = distance > 0.0F ? 1.0F : -1.0F;
  const std::int32_t acrossFrom = cellIndex(across - (radius / 2.0F));
  const std::int32_t acrossTo = cellIndex(across + (radius / 2.0F));

  const float front = along + (sign * radius);
  const std::int32_t first = cellIndex(front);
  if (lineBlocked(collision, axis, first, acrossFrom, acrossTo)) {
    return true; // already touching: the original cancels the move toward it
  }
  const std::int32_t last = cellIndex(front + distance);
  const std::int32_t step = sign > 0.0F ? 1 : -1;
  for (std::int32_t index = first + step; index != last + step; index += step) {
    if (lineBlocked(collision, axis, index, acrossFrom, acrossTo)) {
      const float stop = step > 0 ? cellStart(index) - StopGap : cellStart(index + 1) + StopGap;
      along = stop - (sign * radius);
      return true;
    }
  }
  along += distance;
  return false;
}

bool nearWall(const MapCollision& collision, CollisionCell cell) {
  for (int dz = -1; dz <= 1; ++dz) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (collision.isBlocked({.column = cell.column + dx, .row = cell.row + dz})) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

// map.c: entityCheckCollision tests a whole predicted step and collisionGrace cancels the blocked
// parts; this moves X then Z and stops short of the wall, so no tick length lets a step cross a
// cell (docs/reference/campo.md).
GridMove moveOnGrid(const MapCollision& collision, glm::vec3 position, glm::vec2 displacement,
                    float radius) {
  GridMove move{.position = position};
  move.blockedX = moveAxis(collision, move.position, Axis::X, displacement.x, radius);
  move.blockedZ = moveAxis(collision, move.position, Axis::Z, displacement.y, radius);
  return move;
}

bool stepCollides(const MapCollision& collision, glm::vec3 position, glm::vec2 step, float radius) {
  constexpr float Still = 0.5F;
  const float x = position.x + step.x;
  const float z = position.z + step.y;
  if (std::abs(step.x) >= Still) {
    const float front = x + std::copysign(radius, step.x);
    if (lineBlocked(collision, Axis::X, cellIndex(front), cellIndex(z - (radius / 2.0F)),
                    cellIndex(z + (radius / 2.0F)))) {
      return true;
    }
  }
  if (std::abs(step.y) >= Still) {
    const float front = z + std::copysign(radius, step.y);
    return lineBlocked(collision, Axis::Z, cellIndex(front), cellIndex(x - (radius / 2.0F)),
                       cellIndex(x + (radius / 2.0F)));
  }
  return false;
}

bool fitsOnGrid(const MapCollision& collision, glm::vec3 position, float radius) {
  const std::int32_t xFrom = cellIndex(position.x - (radius / 2.0F));
  const std::int32_t xTo = cellIndex(position.x + (radius / 2.0F));
  const std::int32_t zFrom = cellIndex(position.z - (radius / 2.0F));
  const std::int32_t zTo = cellIndex(position.z + (radius / 2.0F));
  return !lineBlocked(collision, Axis::X, cellIndex(position.x - radius), zFrom, zTo) &&
         !lineBlocked(collision, Axis::X, cellIndex(position.x + radius), zFrom, zTo) &&
         !lineBlocked(collision, Axis::Z, cellIndex(position.z - radius), xFrom, xTo) &&
         !lineBlocked(collision, Axis::Z, cellIndex(position.z + radius), xFrom, xTo) &&
         !collision.isBlocked(assets::collisionCellAt(position));
}

bool isLineClear(const MapCollision& collision, glm::vec3 from, glm::vec3 to, float halfWidth) {
  const glm::vec2 start{from.x, from.z};
  const glm::vec2 delta = glm::vec2{to.x, to.z} - start;
  const float length = glm::length(delta);
  const glm::vec2 side = length > 0.0F ? glm::vec2{-delta.y, delta.x} / length : glm::vec2{0.0F};
  const int samples = static_cast<int>(std::ceil(length / SightStep));
  for (int line = 0; line < SightLines; ++line) {
    const float offset = halfWidth * ((2.0F * static_cast<float>(line) / (SightLines - 1)) - 1.0F);
    for (int sample = 0; sample <= samples; ++sample) {
      const float t =
          samples == 0 ? 0.0F : static_cast<float>(sample) / static_cast<float>(samples);
      const glm::vec2 point = start + (delta * t) + (side * offset);
      if (collision.isBlocked(assets::collisionCellAt({point.x, 0.0F, point.y}))) {
        return false;
      }
    }
  }
  return true;
}

namespace {

// The path search's points, every half cell from the grid's corner: point (u, v) is at x = u * 50 -
// 5000 and z = v * 50 - 5000. Even indices lie on cell edges, odd ones in the middle of a cell.
constexpr std::int32_t LatticeSize = (MapCollision::Size * 2) + 1;
constexpr float LatticeStep = CollisionCellSize / 2.0F;
constexpr std::size_t LatticeCount = static_cast<std::size_t>(LatticeSize) * LatticeSize;
constexpr std::array<std::array<int, 2>, 8> LatticeSteps = {
    {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
constexpr int StraightCost = 10;
constexpr int DiagonalCost = 14;
constexpr int WallCost = 8;

struct LatticePoint {
  std::int32_t u = 0;
  std::int32_t v = 0;
  bool operator==(const LatticePoint&) const = default;
};

bool insideLattice(LatticePoint point) {
  return point.u >= 0 && point.v >= 0 && point.u < LatticeSize && point.v < LatticeSize;
}

std::size_t latticeIndex(LatticePoint point) {
  return (static_cast<std::size_t>(point.v) * LatticeSize) + static_cast<std::size_t>(point.u);
}

LatticePoint latticeAt(std::size_t index) {
  return {.u = static_cast<std::int32_t>(index % LatticeSize),
          .v = static_cast<std::int32_t>(index / LatticeSize)};
}

glm::vec3 latticePosition(LatticePoint point) {
  constexpr float Origin = static_cast<float>(GridOrigin) * CollisionCellSize;
  return {(static_cast<float>(point.u) * LatticeStep) - Origin, 0.0F,
          (static_cast<float>(point.v) * LatticeStep) - Origin};
}

LatticePoint centreOf(CollisionCell cell) {
  return {.u = (cell.column * 2) + 1, .v = (cell.row * 2) + 1};
}

// The cells a point touches: one in the middle of a cell, two on an edge, four on a corner.
template <typename Visit>
bool allTouched(LatticePoint point, Visit visit) {
  const std::int32_t columnFrom = point.u % 2 == 1 ? point.u / 2 : (point.u / 2) - 1;
  const std::int32_t columnTo = point.u / 2;
  const std::int32_t rowFrom = point.v % 2 == 1 ? point.v / 2 : (point.v / 2) - 1;
  const std::int32_t rowTo = point.v / 2;
  for (std::int32_t row = rowFrom; row <= rowTo; ++row) {
    for (std::int32_t column = columnFrom; column <= columnTo; ++column) {
      if (!visit(CollisionCell{.column = column, .row = row})) {
        return false;
      }
    }
  }
  return true;
}

class PathSearch {
public:
  PathSearch(const MapCollision& collision, std::span<const CollisionCell> goals, float radius,
             const CellFilter& passable)
      : collision_(&collision), goals_(goals), radius_(radius), passable_(&passable),
        usable_(LatticeCount, Unknown), steps_(LatticeCount * 4, Unknown) {}

  std::vector<glm::vec3> run(glm::vec3 from) {
    std::vector<LatticePoint> goalPoints;
    for (const CollisionCell goal : goals_) {
      if (MapCollision::contains(goal) && !collision_->isBlocked(goal)) {
        goalPoints.push_back(centreOf(goal));
      }
    }
    if (goalPoints.empty()) {
      return {};
    }
    const auto heuristic = [&](LatticePoint point) {
      int best = std::numeric_limits<int>::max();
      for (const LatticePoint goal : goalPoints) {
        const int du = std::abs(point.u - goal.u);
        const int dv = std::abs(point.v - goal.v);
        best = std::min(best, (StraightCost * std::max(du, dv)) +
                                  ((DiagonalCost - StraightCost) * std::min(du, dv)));
      }
      return best;
    };
    const auto isGoal = [&](LatticePoint point) {
      return std::ranges::find(goalPoints, point) != goalPoints.end();
    };

    std::vector<int> cost(LatticeCount, Unvisited);
    std::vector<std::size_t> cameFrom(LatticeCount, NoPoint);
    using Entry = std::pair<int, std::size_t>; // estimated total cost, lattice index
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;

    // Start from the points around `from` that it walks straight to.
    constexpr auto Origin = static_cast<float>(GridOrigin * 2);
    const auto nearU = static_cast<std::int32_t>(std::floor((from.x / LatticeStep) + Origin));
    const auto nearV = static_cast<std::int32_t>(std::floor((from.z / LatticeStep) + Origin));
    // A character that stands closer to a wall than its probe (a map's arrival point can) walks
    // off it, the probe sliding along the wall: when no point around is reached straight, the
    // usable ones are; and when none of those is usable either (a battle's retreat can leave the
    // player against a wall, its probe on the wall's cells), the usable ones a step further out.
    struct SeedPass {
      bool straight;
      std::int32_t from;
      std::int32_t to;
    };
    for (const SeedPass pass : {SeedPass{.straight = true, .from = 0, .to = 1},
                                SeedPass{.straight = false, .from = 0, .to = 1},
                                SeedPass{.straight = false, .from = -1, .to = 2}}) {
      const bool straight = pass.straight;
      for (std::int32_t dv = pass.from; dv <= pass.to; ++dv) {
        for (std::int32_t du = pass.from; du <= pass.to; ++du) {
          const LatticePoint seed{.u = nearU + du, .v = nearV + dv};
          if (!insideLattice(seed) || !usable(seed, isGoal(seed))) {
            continue;
          }
          const glm::vec3 position = latticePosition(seed);
          const GridMove move =
              moveOnGrid(*collision_, from, {position.x - from.x, position.z - from.z}, radius_);
          if (straight && (move.blockedX || move.blockedZ)) {
            continue;
          }
          const int seedCost = static_cast<int>(std::lround(
              glm::distance(from, position) / LatticeStep * static_cast<float>(StraightCost)));
          const std::size_t index = latticeIndex(seed);
          cost[index] = seedCost;
          open.emplace(seedCost + heuristic(seed), index);
        }
      }
      if (!open.empty()) {
        break;
      }
    }

    std::optional<std::size_t> reached;
    while (!open.empty()) {
      const auto [estimate, index] = open.top();
      open.pop();
      const LatticePoint point = latticeAt(index);
      const int here = cost[index];
      if (estimate - heuristic(point) > here) {
        continue; // a stale entry
      }
      if (isGoal(point)) {
        reached = index;
        break;
      }
      for (const auto& [du, dv] : LatticeSteps) {
        const LatticePoint next{.u = point.u + du, .v = point.v + dv};
        if (!insideLattice(next)) {
          continue;
        }
        const bool goal = isGoal(next);
        if (!usable(next, goal) || !canStep(point, next, goal)) {
          continue;
        }
        const bool diagonal = du != 0 && dv != 0;
        const int nextCost =
            here + (diagonal ? DiagonalCost : StraightCost) + (nearWalls(next) ? WallCost : 0);
        const std::size_t nextIndex = latticeIndex(next);
        if (cost[nextIndex] == Unvisited || nextCost < cost[nextIndex]) {
          cost[nextIndex] = nextCost;
          cameFrom[nextIndex] = index;
          open.emplace(nextCost + heuristic(next), nextIndex);
        }
      }
    }
    if (!reached) {
      return {};
    }
    std::vector<glm::vec3> path;
    for (std::size_t index = *reached; index != NoPoint; index = cameFrom[index]) {
      path.push_back(latticePosition(latticeAt(index)));
    }
    std::ranges::reverse(path);
    if (path.size() > 1 && glm::distance(path.front(), from) < 1e-3F) {
      path.erase(path.begin());
    }
    return path;
  }

private:
  static constexpr std::int8_t Unknown = -1;
  static constexpr int Unvisited = -1;
  static constexpr std::size_t NoPoint = std::numeric_limits<std::size_t>::max();

  [[nodiscard]] bool cellUsable(CollisionCell cell) const {
    if (!MapCollision::contains(cell) || collision_->isBlocked(cell)) {
      return false;
    }
    return !*passable_ || (*passable_)(cell) || std::ranges::find(goals_, cell) != goals_.end();
  }

  bool usable(LatticePoint point, bool goal) {
    if (goal) {
      return true;
    }
    std::int8_t& known = usable_[latticeIndex(point)];
    if (known == Unknown) {
      known = static_cast<std::int8_t>(
          allTouched(point, [&](CollisionCell cell) { return cellUsable(cell); }));
    }
    return known == 1;
  }

  [[nodiscard]] bool nearWalls(LatticePoint point) const {
    return !allTouched(point, [&](CollisionCell cell) { return !nearWall(*collision_, cell); });
  }

  // Whether the character walks from one point to the other, also `leeway` off the line to either
  // side.
  [[nodiscard]] bool walks(glm::vec3 from, glm::vec3 to, float leeway) const {
    // Getting within WaypointReach is enough: a probe that ends exactly on the edge of a wall cell
    // stops the character a hair before the point.
    const glm::vec2 full{to.x - from.x, to.z - from.z};
    const float length = glm::length(full);
    const glm::vec2 delta = full * ((length - WaypointReach) / length);
    const glm::vec2 side = glm::normalize(glm::vec2{-full.y, full.x});
    for (const float offset : {0.0F, leeway, -leeway}) {
      const glm::vec3 start = from + (glm::vec3{side.x, 0.0F, side.y} * offset);
      const GridMove move = moveOnGrid(*collision_, start, delta, radius_);
      if (move.blockedX || move.blockedZ) {
        return false;
      }
      if (leeway == 0.0F) {
        break;
      }
    }
    return true;
  }

  // A straight step, remembered: directions 0 to 3 of LatticeSteps.
  bool straightStep(LatticePoint from, std::size_t direction) {
    std::int8_t& known = steps_[(latticeIndex(from) * 4) + direction];
    if (known == Unknown) {
      const auto [du, dv] = LatticeSteps[direction];
      const LatticePoint to{.u = from.u + du, .v = from.v + dv};
      known = static_cast<std::int8_t>(insideLattice(to) &&
                                       walks(latticePosition(from), latticePosition(to), Leeway));
    }
    return known == 1;
  }

  static std::size_t straightDirection(int du, int dv) {
    if (du != 0) {
      return du > 0 ? 0 : 1;
    }
    return dv > 0 ? 2 : 3;
  }

  // The last step onto a goal's centre needs no leeway: a follower closes in on a point it walks
  // to.
  bool canStep(LatticePoint from, LatticePoint to, bool goal) {
    const int du = to.u - from.u;
    const int dv = to.v - from.v;
    if (du == 0 || dv == 0) {
      return goal ? walks(latticePosition(from), latticePosition(to), 0.0F)
                  : straightStep(from, straightDirection(du, dv));
    }
    // A diagonal step needs both of its corners.
    const LatticePoint alongU{.u = to.u, .v = from.v};
    const LatticePoint alongV{.u = from.u, .v = to.v};
    if (!usable(alongU, false) || !usable(alongV, false)) {
      return false;
    }
    if (goal) {
      return walks(latticePosition(from), latticePosition(alongU), Leeway) &&
             walks(latticePosition(alongU), latticePosition(to), 0.0F) &&
             walks(latticePosition(from), latticePosition(alongV), Leeway) &&
             walks(latticePosition(alongV), latticePosition(to), 0.0F);
    }
    return straightStep(from, straightDirection(du, 0)) &&
           straightStep(alongU, straightDirection(0, dv)) &&
           straightStep(from, straightDirection(0, dv)) &&
           straightStep(alongV, straightDirection(du, 0));
  }

  const MapCollision* collision_;
  std::span<const CollisionCell> goals_;
  float radius_;
  const CellFilter* passable_;
  std::vector<std::int8_t> usable_;
  std::vector<std::int8_t> steps_;
};

} // namespace

std::vector<glm::vec3> findPath(const MapCollision& collision, glm::vec3 from,
                                std::span<const CollisionCell> goals, float radius,
                                const CellFilter& passable) {
  return PathSearch{collision, goals, radius, passable}.run(from);
}

} // namespace dwe::game::field
