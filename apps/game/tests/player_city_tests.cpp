#include <doctest/doctest.h>

#include <vector>

#include "player_city.hpp"

using dwe::app::cityTab;
using dwe::game::story::CityResident;

TEST_CASE("the City tab groups the residents by level and counts who joined") {
  const std::vector<CityResident> residents{
      {.species = 3, .level = 3, .points = 1, .recruited = true},
      {.species = 5, .level = 4, .points = 2, .recruited = false},
      {.species = 12, .level = 5, .points = 3, .recruited = true},
      {.species = 17, .level = 3, .points = 1, .recruited = false},
      {.species = 11, .level = 4, .points = 1, .recruited = true},
  };
  const auto tab = cityTab(residents);
  // Rookies, champions, ultimates; by number inside each group, as the rules list them.
  CHECK(tab.species == "3,17,5,11,12");
  CHECK(tab.levels == "3,3,4,4,5");
  CHECK(tab.points == "1,1,2,1,3");
  CHECK(tab.recruited == "1,0,0,1,1");
  CHECK(tab.residents == 3);
  CHECK(tab.total == 5);
}

TEST_CASE("a City tab without rules is empty") {
  const auto tab = cityTab({});
  CHECK(tab.species.empty());
  CHECK(tab.residents == 0);
  CHECK(tab.total == 0);
}
