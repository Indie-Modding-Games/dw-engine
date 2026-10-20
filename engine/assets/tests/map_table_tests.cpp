#include <dwe/assets/map_table.hpp>

#include <doctest/doctest.h>

#include <string>
#include <string_view>

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

constexpr std::string_view FixtureTable = R"(maps:
- id: "twna01"
  index: 204
  filename: "TWNA01"
  images_8bpp: 2
  images_4bpp: 2
  flags: 140
  sound_id: 12
  fixed_time_of_day: false
  has_npcs: true
  doors: null
  toilet: 1
  loading_name: 2
- id: "room08"
  index: 218
  filename: "ROOM08"
  images_8bpp: 1
  images_4bpp: 0
  flags: 204
  sound_id: 12
  fixed_time_of_day: true
  has_npcs: true
  doors: 0
  toilet: null
  loading_name: 1
- id: "map_219"
  index: 219
  filename: ""
  images_8bpp: 0
  images_4bpp: 0
  flags: 0
  sound_id: 0
  fixed_time_of_day: false
  has_npcs: false
  doors: null
  toilet: null
  loading_name: 70
doors:
- index: 0
  doors:
  - {model_id: null, position: {x: 0, y: 0, z: 0}, rotation: 0}
  - {model_id: 9, position: {x: 1925, y: 0, z: -915}, rotation: 1024}
  - {model_id: null, position: {x: 0, y: 0, z: 0}, rotation: 0}
  - {model_id: null, position: {x: 0, y: 0, z: 0}, rotation: 0}
  - {model_id: null, position: {x: 0, y: 0, z: 0}, rotation: 0}
  - {model_id: 10, position: {x: -1950, y: 0, z: -520}, rotation: -1024}
toilets:
- {index: 0, x1: -196, z1: -1831, x2: -392, z2: -976}
- {index: 1, x1: -2852, z1: -957, x2: -3319, z2: -714}
map_names:
- "      Native Forest     "
- "      Jijimon's house   "
- "        File City       "
light_updates:
- {map: "twnb04", mode: 1, start_id: 49, count: 2, trigger: null}
- {map: 300, mode: 0, start_id: 1, count: 1, trigger: 7}
)";

} // namespace

TEST_CASE("the map table reads every map and its tables") {
    const auto table = dwe::assets::parseMapTable(FixtureTable);
    REQUIRE_MESSAGE(table.has_value(), errorOf(table));
    REQUIRE(table->maps.size() == 3);

    const dwe::assets::MapEntry* town = table->find("twna01");
    REQUIRE(town != nullptr);
    CHECK(town->index == 204);
    CHECK(town->filename == "TWNA01");
    CHECK(town->flags == 140);
    CHECK_FALSE(town->fixedTimeOfDay);
    CHECK(town->toilet == std::optional<std::int32_t>{1});
    CHECK_FALSE(town->doors.has_value());
    CHECK(table->displayName(*town) == "File City");
    CHECK(table->doorsOf(*town) == nullptr);

    const dwe::assets::MapEntry* house = table->findByIndex(218);
    REQUIRE(house != nullptr);
    CHECK(house->id == "room08");
    CHECK(house->fixedTimeOfDay);
    CHECK(table->displayName(*house) == "Jijimon's house");
    const dwe::assets::MapDoorSet* doors = table->doorsOf(*house);
    REQUIRE(doors != nullptr);
    CHECK_FALSE(doors->doors[0].modelId.has_value());
    CHECK(doors->doors[1].modelId == std::optional<std::int32_t>{9});
    CHECK(doors->doors[5].rotation == -1024);

    const dwe::assets::MapEntry* empty = table->find("map_219");
    REQUIRE(empty != nullptr);
    CHECK_FALSE(empty->hasFile());
    CHECK(table->displayName(*empty).empty());
    CHECK(table->find("nowhere") == nullptr);
    CHECK(table->findByIndex(5) == nullptr);

    REQUIRE(table->toilets.size() == 2);
    CHECK(table->toilets[1].x2 == -3319);
    REQUIRE(table->lightUpdates.size() == 2);
    CHECK(table->lightUpdates[0].map == "twnb04");
    CHECK_FALSE(table->lightUpdates[0].trigger.has_value());
    CHECK(table->lightUpdates[1].map == "300");
    CHECK(table->lightUpdates[1].trigger == std::optional<std::int32_t>{7});
}

TEST_CASE("a broken map table is refused") {
    SUBCASE("a missing key") {
        std::string text{FixtureTable};
        text.replace(text.find("  sound_id: 12\n"), 15, "");
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
        const std::size_t line = text.find("  - {model_id: 10");
        text.erase(line, text.find('\n', line) + 1 - line);
        const auto table = dwe::assets::parseMapTable(text);
        REQUIRE_FALSE(table.has_value());
        CHECK(table.error().message.find("6 items") != std::string::npos);
    }
}
