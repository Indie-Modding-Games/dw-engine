#include <dwe/assets/fishing_table.hpp>
#include <dwe/game/inventory/stacking.hpp>
#include <dwe/session/fishing_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "adapter_fixture.hpp"
#include "game/inventory/tests/fixture.hpp"
#include "session_fixture.hpp"

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

namespace {

// A made-up fishing table in the shape dwx extracts: one fish kind and the sea serpent, one
// shoreline, one lake on the map "lake".
constexpr std::string_view LakeTable = R"(bait_chances: []
bite_chances: {first_depth: 38, rows: [[10, 20, 0, 5, 0, 0]], unused: []}
kinds:
- {index: 0, unknown_0: 0, size_min: 20, size_max: 60, move_delay_min: 5, move_delay_max: 20, bait: null, unknown_16: 0}
- {index: 1, unknown_0: 0, size_min: 300, size_max: 300, move_delay_min: 5, move_delay_max: 20, bait: null, unknown_16: 0}
shorelines:
- {axis: x, points: [{x: -300, y: 0}, {x: 300, y: 0}]}
maps:
- map: lake
  unknown_2: 0
  scale: 14336
  unknown_8: 70
  fish_scale_small: 341
  fish_scale_large: 170
  seadramon_times: [800, 1700]
  spots:
  - {unknown_0: [30, 0, 0, 0], unknown_4: 255, points: [{x: -100, y: -100}, {x: 100, y: -100}, {x: 0, y: 100}], unknown_d: 0}
  unknown_1c: 0
  shoreline: 0
  cast_range: 200
  unknown_2a: 0
  sprite_file: "\\ETCDAT\\FISHING2.TIM"
  seadramon_waypoints: [{x: -100, y: 0}, {x: 100, y: 0}]
)";

} // namespace

TEST_CASE("the partner's natural death takes the lake's water away until a map loads again") {
  using namespace dwe::session;
  const testing::TestPacks packs;
  const dwe::game::evolution::testing::TableFixture forms;
  auto evolution = testing::evolutionAdapter(packs, forms);
  auto bag =
      std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                              .items = items::fixtureItems(),
                                                              .effects = items::fixtureEffects(),
                                                              .evolution = evolution.get()});
  auto story = std::make_unique<StoryAdapter>(StoryAdapterDesc{.files = &packs.files()});
  auto table = dwe::assets::parseFishingTable(LakeTable);
  REQUIRE_MESSAGE(table.has_value(), testing::errorOf(table));
  auto water = std::make_unique<FishingAdapter>(*bag, *story, std::move(*table));
  FishingAdapter& fishing = *water;
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(evolution));
  systems.push_back(std::move(bag));
  systems.push_back(std::move(story));
  systems.push_back(std::move(water));
  const auto session = testing::newGameWith(packs, std::move(systems));
  fishing.enterMap("lake");
  REQUIRE(fishing.fishing() != nullptr);
  // partnerTickDying: deinitializeFishing (FISHING_DATA_PTR NULL); the death's later
  // setFishingDisabled and setFishingEnabled find no water and bring none back.
  fishing.leaveWater();
  fishing.setFishingDisabled();
  fishing.setFishingEnabled();
  CHECK(fishing.fishing() == nullptr);
  CHECK(fishing.map() == nullptr);
  CHECK(fishing.availability() == FishingAvailability::None);
  // The lake loads again (checkFishingMap): a fresh water.
  fishing.enterMap("lake");
  CHECK(fishing.fishing() != nullptr);
}
