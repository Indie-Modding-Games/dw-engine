#include <dwe/core/json_schema.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/weather/climate.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readText(const std::filesystem::path& path) {
  std::ifstream file{path};
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

dwe::core::JsonSchema weatherSchema() {
  auto tree = dwe::core::parseYaml(
      readText(std::filesystem::path{DWE_SCHEMA_DIR} / "field_weather.schema.json"));
  REQUIRE(tree.has_value());
  auto schema = dwe::core::JsonSchema::compile(*tree);
  REQUIRE(schema.has_value());
  return std::move(*schema);
}

// The schema's problems with a document, one per line.
std::string problemsOf(const dwe::core::JsonSchema& schema, const std::string& yaml) {
  auto document = dwe::core::parseYaml(yaml);
  REQUIRE(document.has_value());
  std::string problems;
  for (const dwe::core::SchemaProblem& problem : schema.check(*document).problems) {
    problems +=
        std::format("{}:{}: {}: {}\n", problem.line, problem.column, problem.path, problem.message);
  }
  return problems;
}

} // namespace

TEST_CASE("the pack's field weather follows its schema and loads") {
  const std::string pack =
      readText(std::filesystem::path{DWE_GAME_PACK} / "field" / "weather.yaml");
  const dwe::core::JsonSchema schema = weatherSchema();
  CHECK(problemsOf(schema, pack).empty());
  CHECK(dwe::game::weather::parseClimateRules(pack).has_value());
  // The schema refuses what the loader refuses: a front without its delay, a neighbour that is
  // not an id.
  const std::string head = "period_hours: 3\nkeep_chance: 0.5\nfade_seconds: 8\n"
                           "areas:\n  - id: a\n    maps: [a*]\n    weights: { clear: 1 }\n";
  CHECK(problemsOf(schema, head).empty());
  CHECK_FALSE(problemsOf(schema, head + "fronts: { chance: 0.5, reach: 1 }\n").empty());
  CHECK_FALSE(problemsOf(schema, head + "    neighbours: [\"Not An Id\"]\n").empty());
}
