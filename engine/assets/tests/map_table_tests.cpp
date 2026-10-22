#include <dwe/assets/map_table.hpp>

#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "map_documents.hpp"

using dwe::assets::testing::FixtureTable;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

} // namespace

TEST_CASE("the map table reads every map and its tables") {
  const auto table = dwe::assets::parseMapTable(FixtureTable);
  REQUIRE_MESSAGE(table.has_value(), errorOf(table));
  REQUIRE(table->maps.size() == 3);

  const dwe::assets::MapEntry* town = table->find("twna01");
  REQUIRE(town != nullptr);
  CHECK(town->index == 104);
  CHECK(town->filename == "TWNA01");
  CHECK(town->flags == 137);
  CHECK_FALSE(town->fixedTimeOfDay);
  CHECK(town->toilet == std::optional<std::int32_t>{1});
  CHECK_FALSE(town->doors.has_value());
  CHECK(table->displayName(*town) == "Test Town");
  CHECK(table->doorsOf(*town) == nullptr);

  const dwe::assets::MapEntry* house = table->findByIndex(118);
  REQUIRE(house != nullptr);
  CHECK(house->id == "room08");
  CHECK(house->fixedTimeOfDay);
  CHECK(table->displayName(*house) == "Test Cabin");
  const dwe::assets::MapDoorSet* doors = table->doorsOf(*house);
  REQUIRE(doors != nullptr);
  CHECK_FALSE(doors->doors[0].modelId.has_value());
  CHECK(doors->doors[1].modelId == std::optional<std::int32_t>{4});
  CHECK(doors->doors[5].rotation == -1024);

  const dwe::assets::MapEntry* empty = table->find("map_119");
  REQUIRE(empty != nullptr);
  CHECK_FALSE(empty->hasFile());
  CHECK(table->displayName(*empty).empty());
  CHECK(table->find("nowhere") == nullptr);
  CHECK(table->findByIndex(5) == nullptr);

  REQUIRE(table->toilets.size() == 2);
  CHECK(table->toilets[1].x2 == -3300);
  REQUIRE(table->lightUpdates.size() == 2);
  CHECK(table->lightUpdates[0].map == "twnb04");
  CHECK_FALSE(table->lightUpdates[0].trigger.has_value());
  CHECK(table->lightUpdates[1].map == "300");
  CHECK(table->lightUpdates[1].trigger == std::optional<std::int32_t>{7});
}

TEST_CASE("a broken map table is refused") {
  SUBCASE("a missing key") {
    std::string text{FixtureTable};
    text.replace(text.find("  sound_id: 9\n"), 14, "");
    const auto table = dwe::assets::parseMapTable(text);
    REQUIRE_FALSE(table.has_value());
    CHECK(table.error().message.find("'sound_id'") != std::string::npos);
  }
  SUBCASE("a map listed twice") {
    std::string text{FixtureTable};
    text.replace(text.find("\"room08\""), 8, "\"twna01\"");
    const auto table = dwe::assets::parseMapTable(text);
    REQUIRE_FALSE(table.has_value());
    CHECK(table.error().message.find("listed twice") != std::string::npos);
  }
  SUBCASE("a door set without six doors") {
    std::string text{FixtureTable};
    const std::size_t line = text.find("  - {model_id: 5");
    text.erase(line, text.find('\n', line) + 1 - line);
    const auto table = dwe::assets::parseMapTable(text);
    REQUIRE_FALSE(table.has_value());
    CHECK(table.error().message.find("6 items") != std::string::npos);
  }
}
