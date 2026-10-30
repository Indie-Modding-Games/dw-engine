#include <dwe/assets/card_table.hpp>

#include <doctest/doctest.h>

#include <string>

TEST_CASE("a card keeps the palette of its icon, and content without it has none") {
  const auto table = dwe::assets::parseCardTable(R"(prices: [5000, 100]
cards:
- {index: 0, digimon: "agum", sprite_id: 1, merit_value: 100, icon_palette: 7}
- {index: 1, digimon: null, sprite_id: 0, merit_value: 0}
)");
  REQUIRE_MESSAGE(table.has_value(), (table ? std::string{} : table.error().message));
  REQUIRE(table->find(0) != nullptr);
  CHECK(table->find(0)->iconPalette == 7);
  REQUIRE(table->find(1) != nullptr);
  CHECK_FALSE(table->find(1)->iconPalette.has_value());
}
