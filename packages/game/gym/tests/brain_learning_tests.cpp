#include <dwe/game/gym/brain_learning.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <optional>

namespace {

using dwe::game::gym::BrainLearning;
using dwe::game::gym::OrderRows;

BrainLearning learning() {
  BrainLearning rules;
  rules.orderThresholds = {100, 200, 300, 400, 500};
  rules.moveThresholds = {150, 250};
  rules.moveCap = 999;
  return rules;
}

// The original's 300 order has a second line ("Change target!").
bool secondLine(std::size_t rank) {
  return rank == 2;
}

std::optional<OrderRows> rowsFor(int before, int gain) {
  const BrainLearning rules = learning();
  return dwe::game::gym::orderRows(rules, dwe::game::gym::brainMilestones(rules, before, gain),
                                   secondLine);
}

} // namespace

TEST_CASE("gym.md brains: an order's words take the first row, and 300 adds the second") {
  const auto hundred = rowsFor(95, 8);
  REQUIRE(hundred.has_value());
  CHECK(hundred->first == 0);
  CHECK_FALSE(hundred->second.has_value());

  const auto threeHundred = rowsFor(295, 8);
  REQUIRE(threeHundred.has_value());
  CHECK(threeHundred->first == 2);
  CHECK(threeHundred->second == std::optional<std::size_t>{2});
}

TEST_CASE("gym.md brains: crossing two orders draws the later one's words over the first row") {
  // 290 + 120 crosses 300 and 400: "Hang in there!" over "Get back!", "Change target!" stays.
  const auto both = rowsFor(290, 120);
  REQUIRE(both.has_value());
  CHECK(both->first == 3);
  CHECK(both->second == std::optional<std::size_t>{2});
}

TEST_CASE("gym.md brains: without an order crossed there are no order rows") {
  CHECK_FALSE(rowsFor(140, 15).has_value());
}
