#include <dwe/game/field/grid.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "fixture.hpp"

using dwe::assets::CollisionCell;
using dwe::assets::MapCollision;
using dwe::game::field::findPath;
using dwe::game::field::fitsOnGrid;
using dwe::game::field::isLineClear;
using dwe::game::field::moveOnGrid;
using dwe::game::field::testing::fill;
using dwe::game::field::testing::roomMap;

namespace {

constexpr float Radius = 100.0F;
constexpr std::uint8_t Blocked = MapCollision::BlockedBit;

// Moves in `steps` equal steps; returns the end.
glm::vec3 walk(const MapCollision& collision, glm::vec3 from, glm::vec2 total, int steps) {
  for (int i = 0; i < steps; ++i) {
    from = moveOnGrid(collision, from, total / static_cast<float>(steps), Radius).position;
  }
  return from;
}

} // namespace

TEST_CASE("a move stops at the wall with the original's probe distance") {
  // The room is free from x and z -2000 to 2000; the probe reaches 100 ahead, so the centre stops
  // 100 before.
  const auto map = roomMap("room");
  const auto east = moveOnGrid(map.collision, {0.0F, 0.0F, 0.0F}, {5000.0F, 0.0F}, Radius);
  CHECK(east.blockedX);
  CHECK_FALSE(east.blockedZ);
  CHECK(east.position.x == doctest::Approx(1900.0F).epsilon(1e-4));
  const auto west = moveOnGrid(map.collision, {0.0F, 0.0F, 0.0F}, {-5000.0F, 0.0F}, Radius);
  CHECK(west.position.x == doctest::Approx(-1900.0F).epsilon(1e-4));
  const auto north = moveOnGrid(map.collision, {0.0F, 0.0F, 0.0F}, {0.0F, -5000.0F}, Radius);
  CHECK(north.blockedZ);
  CHECK(north.position.z == doctest::Approx(-1900.0F).epsilon(1e-4));

  // Pushing on against the wall does not move.
  const auto again = moveOnGrid(map.collision, east.position, {50.0F, 0.0F}, Radius);
  CHECK(again.blockedX);
  CHECK(again.position == east.position);
  // Moving away from it does.
  CHECK(moveOnGrid(map.collision, east.position, {-50.0F, 0.0F}, Radius).position.x ==
        doctest::Approx(1850.0F).epsilon(1e-4));
}

TEST_CASE("a diagonal move against a wall slides along it") {
  const auto map = roomMap("room");
  const auto slide = moveOnGrid(map.collision, {1850.0F, 0.0F, 0.0F}, {300.0F, 300.0F}, Radius);
  CHECK(slide.blockedX);
  CHECK_FALSE(slide.blockedZ);
  CHECK(slide.position.x == doctest::Approx(1900.0F).epsilon(1e-4));
  CHECK(slide.position.z == doctest::Approx(300.0F));

  // Into a corner, both components stop.
  const auto corner = moveOnGrid(map.collision, {0.0F, 0.0F, 0.0F}, {9000.0F, 9000.0F}, Radius);
  CHECK(corner.blockedX);
  CHECK(corner.blockedZ);
  CHECK(corner.position.x == doctest::Approx(1900.0F).epsilon(1e-4));
  CHECK(corner.position.z == doctest::Approx(1900.0F).epsilon(1e-4));
}

TEST_CASE("the probe is radius ahead and half the radius to the sides") {
  auto map = roomMap("room");
  fill(map, {.column = 50, .row = 50}, {.column = 50, .row = 50}, Blocked); // x and z from 0 to 100

  // Along x at z 50: the cell is in the probe's side span [0, 100], so it stops 100 before x 0.
  CHECK(moveOnGrid(map.collision, {-500.0F, 0.0F, 50.0F}, {1000.0F, 0.0F}, Radius).position.x ==
        doctest::Approx(-100.0F).epsilon(1e-3));
  // At z 140 the side span [90, 190] still touches row 50; at z 150 it is [100, 200] and passes.
  CHECK(moveOnGrid(map.collision, {-500.0F, 0.0F, 140.0F}, {1000.0F, 0.0F}, Radius).blockedX);
  CHECK_FALSE(moveOnGrid(map.collision, {-500.0F, 0.0F, 150.0F}, {1000.0F, 0.0F}, Radius).blockedX);
  // From the other side the stop is 100 after x 100.
  CHECK(moveOnGrid(map.collision, {500.0F, 0.0F, 50.0F}, {-1000.0F, 0.0F}, Radius).position.x ==
        doctest::Approx(200.0F).epsilon(1e-3));
}

TEST_CASE("no step is long enough to go through a wall") {
  auto map = roomMap("room");
  fill(map, {.column = 60, .row = 30}, {.column = 60, .row = 69},
       Blocked); // a thin wall at x 1000 to 1100
  for (const int steps : {1, 3, 30, 600}) {
    const glm::vec3 end = walk(map.collision, {0.0F, 0.0F, 0.0F}, {3000.0F, 0.0F}, steps);
    CHECK(end.x == doctest::Approx(900.0F).epsilon(1e-4));
  }
  // The same trajectory in steps of any size ends at the same place.
  const glm::vec3 coarse = walk(map.collision, {-1000.0F, 0.0F, -1000.0F}, {2500.0F, 2200.0F}, 2);
  const glm::vec3 fine = walk(map.collision, {-1000.0F, 0.0F, -1000.0F}, {2500.0F, 2200.0F}, 500);
  CHECK(coarse.x == doctest::Approx(900.0F).epsilon(1e-4));
  CHECK(glm::distance(coarse, fine) < 0.1F);
}

TEST_CASE("cells outside the grid are walls") {
  MapCollision open; // every cell free
  const auto edge = moveOnGrid(open, {0.0F, 0.0F, 0.0F}, {9000.0F, 0.0F}, Radius);
  CHECK(edge.blockedX);
  CHECK(edge.position.x == doctest::Approx(4900.0F).epsilon(1e-4));
}

TEST_CASE("a character fits where no probe touches a wall") {
  const auto map = roomMap("room");
  CHECK(fitsOnGrid(map.collision, {0.0F, 0.0F, 0.0F}, Radius));
  CHECK(fitsOnGrid(map.collision, {1899.0F, 0.0F, 0.0F}, Radius));
  CHECK_FALSE(fitsOnGrid(map.collision, {1901.0F, 0.0F, 0.0F}, Radius));
  CHECK_FALSE(fitsOnGrid(map.collision, {0.0F, 0.0F, -1950.0F}, Radius));
}

TEST_CASE("sight lines see through free cells only") {
  auto map = roomMap("room");
  fill(map, {.column = 50, .row = 30}, {.column = 50, .row = 60},
       Blocked); // a wall at x 0 to 100, open from z 1100
  CHECK_FALSE(isLineClear(map.collision, {-500.0F, 0.0F, 0.0F}, {500.0F, 0.0F, 0.0F}, 0.0F));
  CHECK(isLineClear(map.collision, {-500.0F, 0.0F, 1500.0F}, {500.0F, 0.0F, 1500.0F}, 0.0F));
  // A wide corridor of sight touches the wall's end at z 1100.
  CHECK_FALSE(
      isLineClear(map.collision, {-500.0F, 0.0F, 1250.0F}, {500.0F, 0.0F, 1250.0F}, 200.0F));
  CHECK(isLineClear(map.collision, {-500.0F, 0.0F, 1350.0F}, {500.0F, 0.0F, 1350.0F}, 200.0F));
}

namespace {

// Walks a path from `from` with moveOnGrid, also `offset` off each step's line; returns where it
// ended.
glm::vec3 followPath(const MapCollision& collision, glm::vec3 from,
                     const std::vector<glm::vec3>& path, float offset) {
  glm::vec3 position = from;
  for (const glm::vec3 waypoint : path) {
    const glm::vec2 delta{waypoint.x - position.x, waypoint.z - position.z};
    const glm::vec2 side =
        glm::length(delta) > 0.0F ? glm::normalize(glm::vec2{-delta.y, delta.x}) : glm::vec2{};
    const glm::vec3 start = position + (glm::vec3{side.x, 0.0F, side.y} * offset);
    const auto move = moveOnGrid(collision, start, delta, Radius);
    CHECK_FALSE(move.blockedX);
    CHECK_FALSE(move.blockedZ);
    position = waypoint;
  }
  return position;
}

} // namespace

TEST_CASE("paths go around walls through passages a character fits in") {
  auto map = roomMap("room");
  fill(map, {.column = 50, .row = 30}, {.column = 50, .row = 65}, Blocked); // gap at rows 66 to 69
  const glm::vec3 from{-1000.0F, 0.0F, 0.0F};
  const std::vector<CollisionCell> goal{{.column = 60, .row = 50}};
  const auto path = findPath(map.collision, from, goal, Radius);
  REQUIRE_FALSE(path.empty());
  CHECK(path.back() == dwe::assets::collisionCellCenter(goal[0]));
  bool throughGap = false;
  glm::vec3 previous = from;
  for (const glm::vec3 waypoint : path) {
    CHECK(glm::distance(previous, waypoint) <= 75.0F); // half-cell steps, or a diagonal one
    throughGap = throughGap || (dwe::assets::collisionCellAt(waypoint).column == 50);
    previous = waypoint;
  }
  CHECK(throughGap);
  // Walkable on the line and off it by the leeway to either side.
  for (const float offset : {0.0F, dwe::game::field::Leeway, -dwe::game::field::Leeway}) {
    CHECK(dwe::assets::collisionCellAt(followPath(map.collision, from, path, offset)) == goal[0]);
  }

  // A wall, a closed passage and a filter that forbids the way find nothing.
  CHECK(findPath(map.collision, from, std::vector<CollisionCell>{{.column = 50, .row = 40}}, Radius)
            .empty());
  const auto avoid = [](CollisionCell cell) { return cell.column != 50; };
  CHECK(findPath(map.collision, from, goal, Radius, avoid).empty());
  // A passage one cell wide is too narrow; two cells is enough.
  fill(map, {.column = 50, .row = 66}, {.column = 50, .row = 68}, Blocked);
  CHECK(findPath(map.collision, from, goal, Radius).empty());
  fill(map, {.column = 50, .row = 68}, {.column = 50, .row = 68}, 0);
  CHECK_FALSE(findPath(map.collision, from, goal, Radius).empty());
}

TEST_CASE("a path goes to the nearest of several goals") {
  auto map = roomMap("room");
  const glm::vec3 from{0.0F, 0.0F, 0.0F};
  const std::vector<CollisionCell> goals{{.column = 60, .row = 50}, {.column = 45, .row = 50}};
  const auto path = findPath(map.collision, from, goals, Radius);
  REQUIRE_FALSE(path.empty());
  CHECK(path.back() == dwe::assets::collisionCellCenter(goals[1]));
  // Starting on a goal's centre, the path is just that point.
  const glm::vec3 centre = dwe::assets::collisionCellCenter(goals[0]);
  CHECK(findPath(map.collision, centre, goals, Radius) == std::vector<glm::vec3>{centre});
}
