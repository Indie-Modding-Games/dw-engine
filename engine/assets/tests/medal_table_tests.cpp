#include <dwe/assets/medal_table.hpp>

#include <doctest/doctest.h>

#include <string>
#include <string_view>

namespace {

// Made-up rows in the shape dwx extracts.
constexpr std::string_view MedalDocument = R"(medals:
- id: "first_cup"
  index: 0
  name: "First Cup"
  description:
  - "Line one"
  - "Line two"
  - ""
- id: "short"
  index: 4
  name: "Short"
  description:
  - "Only one"
)";

} // namespace

TEST_CASE("the medal table reads each medal's name and its three lines") {
  const auto table = dwe::assets::parseMedalTable(MedalDocument);
  REQUIRE_MESSAGE(table.has_value(), (table ? std::string{} : table.error().message));
  REQUIRE(table->medals.size() == 2);
  const auto* first = table->find(0);
  REQUIRE(first != nullptr);
  CHECK(first->name == "First Cup");
  CHECK(first->description[1] == "Line two");
  const auto* shorter = table->find(4);
  REQUIRE(shorter != nullptr);
  CHECK(shorter->description[0] == "Only one");
  CHECK(shorter->description[2].empty());
  CHECK(table->find(1) == nullptr);
}

TEST_CASE("the medal table refuses a medal listed twice or with four lines") {
  CHECK_FALSE(dwe::assets::parseMedalTable(R"(medals:
- {id: "a", index: 1, name: "A", description: []}
- {id: "b", index: 1, name: "B", description: []}
)")
                  .has_value());
  CHECK_FALSE(dwe::assets::parseMedalTable(R"(medals:
- {id: "a", index: 1, name: "A", description: ["1", "2", "3", "4"]}
)")
                  .has_value());
}
