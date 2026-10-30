#include <dwe/game/field/player_trail.hpp>

#include <doctest/doctest.h>

#include <vector>

using dwe::assets::CollisionCell;
using dwe::assets::MapCollision;
using dwe::game::field::isCellLineBlocked;
using dwe::game::field::PlayerTrail;

namespace {

void block(MapCollision& collision, std::int32_t column, std::int32_t fromRow, std::int32_t toRow) {
  for (std::int32_t row = fromRow; row <= toRow; ++row) {
    collision.setValue({.column = column, .row = row}, MapCollision::BlockedBit);
  }
}

} // namespace

TEST_CASE("a line of cells is blocked by a wall cell on it, not by its start") {
  MapCollision collision;
  block(collision, 50, 40, 60);
  CHECK(isCellLineBlocked(collision, {.column = 45, .row = 50}, {.column = 55, .row = 52}));
  CHECK_FALSE(isCellLineBlocked(collision, {.column = 45, .row = 50}, {.column = 49, .row = 30}));
  CHECK_FALSE(isCellLineBlocked(collision, {.column = 50, .row = 50}, {.column = 51, .row = 50}));
  CHECK(isCellLineBlocked(collision, {.column = 51, .row = 50}, {.column = 50, .row = 50}));
}

TEST_CASE("the way back keeps the cell where the line to the entry broke and forgets it in sight") {
  MapCollision collision;
  // A wall between column 49 and 51 from row 45 up: walking from the entry (52, 40) up past it and
  // round to the left loses sight of the entry.
  block(collision, 50, 45, 70);
  PlayerTrail trail;
  const CollisionCell entry{.column = 52, .row = 40};
  trail.enter(entry);
  CHECK(trail.start() == entry);
  CHECK(trail.newestFirst().empty());
  std::vector<CollisionCell> walk;
  for (std::int32_t row = 41; row <= 72; ++row) {
    walk.push_back({.column = 52, .row = row});
  }
  for (std::int32_t column = 51; column >= 46; --column) {
    walk.push_back({.column = column, .row = 72});
  }
  for (std::int32_t row = 71; row >= 60; --row) {
    walk.push_back({.column = 46, .row = row});
  }
  for (const CollisionCell cell : walk) {
    trail.tick(cell, collision);
  }
  const std::vector<CollisionCell> cells = trail.newestFirst();
  REQUIRE_FALSE(cells.empty());
  // The first cell kept is the last one that still saw the entry.
  CHECK_FALSE(isCellLineBlocked(collision, cells.back(), entry));

  // A battle's run back that did not reach the oldest cell keeps only that one.
  PlayerTrail kept = trail;
  kept.keepOldest(1);
  REQUIRE(kept.newestFirst().size() == 1);
  CHECK(kept.newestFirst().front() == cells.back());
  kept.keepOldest(0);
  CHECK(kept.newestFirst().empty());

  // Back in sight of the entry, it keeps nothing.
  trail.tick({.column = 52, .row = 44}, collision);
  CHECK(trail.newestFirst().empty());
}
