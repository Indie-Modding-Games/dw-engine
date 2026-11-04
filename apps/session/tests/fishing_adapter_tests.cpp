#include <dwe/game/inventory/stacking.hpp>
#include <dwe/session/fishing_adapter.hpp>

#include <doctest/doctest.h>

#include <optional>
#include <vector>

#include "game/inventory/tests/fixture.hpp"

namespace items = dwe::game::inventory::testing;
using dwe::game::inventory::Stack;

TEST_CASE("the bait menu lists every food slot in the bag's order with its own count") {
  // The made-up table's food (sorting value 2) is the bandage and the toilet; the tonic is not.
  const std::vector<std::optional<Stack>> bag{
      Stack{.item = items::handleOf(items::Toilet), .count = 3},
      Stack{.item = items::handleOf(items::Tonic), .count = 5},
      std::nullopt,
      Stack{.item = items::handleOf(items::Bandage), .count = 5},
      Stack{.item = items::handleOf(items::Toilet), .count = 2},
      Stack{.item = items::handleOf(items::Bandage), .count = 0},
  };
  const std::vector<Stack> rows = dwe::session::baitRows(bag, items::fixtureItems());
  const std::vector<Stack> expected{
      Stack{.item = items::handleOf(items::Toilet), .count = 3},
      Stack{.item = items::handleOf(items::Bandage), .count = 5},
      Stack{.item = items::handleOf(items::Toilet), .count = 2},
  };
  CHECK(rows == expected);
}

TEST_CASE("a bag without food leaves the bait menu empty") {
  const std::vector<std::optional<Stack>> bag{
      Stack{.item = items::handleOf(items::Tonic), .count = 1}, std::nullopt};
  CHECK(dwe::session::baitRows(bag, items::fixtureItems()).empty());
}
