#include <dwe/assets/evolution_chart.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/ruleset/species_extra.hpp>

#include <doctest/doctest.h>

#include <optional>
#include <string>

#include "player_chart.hpp"

namespace dwe::app {

namespace {

assets::EvolutionChart smallChart() {
  assets::EvolutionChart chart;
  chart.image = "texture/chart";
  chart.entries.push_back(
      {.digimon = "leom", .index = 48, .x = 165, .y = 81, .u = 160, .v = 80, .palette = 3});
  chart.entries.push_back(
      {.digimon = "elec", .index = 18, .x = 104, .y = 81, .u = 32, .v = 16, .palette = 0});
  return chart;
}

} // namespace

TEST_CASE("a ruleset's new species gets its cell, with another species' icon, once a path leads "
          "to it") {
  ruleset::SpeciesExtra extra;
  extra.species["panj"] = ruleset::SpeciesPatch{
      .chart = ruleset::ChartCell{.x = 189, .y = 176, .icon = "leom", .palette = 2}};
  assets::EvolutionTable evolution;
  evolution.paths.push_back({.digimon = "elec", .index = 18, .from = {}, .to = {}});
  // No path leads to it: no cell.
  CHECK(chartWithExtraCells(smallChart(), extra, evolution, nullptr).entries.size() == 2);

  evolution.paths.front().to.at(5) = std::string{"panj"};
  const assets::EvolutionChart chart = chartWithExtraCells(smallChart(), extra, evolution, nullptr);
  REQUIRE(chart.entries.size() == 3);
  const assets::ChartCell& cell = chart.entries.back();
  CHECK(cell.digimon == "panj");
  CHECK(cell.x == 189);
  CHECK(cell.y == 176);
  CHECK(cell.u == 160);
  CHECK(cell.v == 80);
  CHECK(cell.palette == 2);

  // A species already on the chart keeps its cell.
  extra.species["leom"] = ruleset::SpeciesPatch{
      .chart = ruleset::ChartCell{.x = 1, .y = 1, .icon = "elec", .palette = 0}};
  evolution.paths.front().to.at(4) = std::string{"leom"};
  CHECK(chartWithExtraCells(smallChart(), extra, evolution, nullptr).entries.size() == 3);
}

} // namespace dwe::app
