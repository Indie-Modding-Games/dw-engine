#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "minimap_items.hpp"
#include "packages/game/inventory/tests/fixture.hpp"

namespace dwe::app {
namespace {

namespace items = game::inventory::testing;

game::field::GroundItem itemAt(std::string_view item, float x, float z) {
  return game::field::GroundItem{
      .item = items::handleOf(item), .cell = {}, .position = {x, 0.0F, z}};
}

} // namespace

TEST_CASE("the minimap marks the drawn items on the ground, where they lie, and none when off") {
  // Slot 1 is empty (picked up) and the field does not draw the bun (say it has no icon of the
  // original's), so the minimap marks two.
  const std::array<std::optional<game::field::GroundItem>, 4> ground{
      itemAt(items::Tonic, 100.0F, -200.0F), std::nullopt, itemAt(items::Bun, 0.0F, 0.0F),
      itemAt(items::Tonic, -50.0F, 75.0F)};
  const GroundItemDrawn drawn = [](assets::ItemHandle item) {
    return item != items::handleOf(items::Bun);
  };
  const std::vector<ui::MinimapMarker> items = minimapItems(true, ground, drawn);
  REQUIRE(items.size() == 2);
  CHECK(items[0].x == 100.0F);
  CHECK(items[0].z == -200.0F);
  CHECK(items[1].x == -50.0F);
  CHECK(items[1].z == 75.0F);
  CHECK(minimapItems(false, ground, drawn).empty());
  CHECK(minimapItems(true, {}, drawn).empty());
}

} // namespace dwe::app
