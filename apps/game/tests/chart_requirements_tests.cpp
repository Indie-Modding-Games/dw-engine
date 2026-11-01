#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/strategies.hpp>
#include <dwe/ruleset/species_extra.hpp>

#include <doctest/doctest.h>

#include <format>
#include <string>
#include <vector>

#include "chart_requirements.hpp"
#include "game/evolution/tests/fixture.hpp"

namespace dwe::app {

namespace {

using game::evolution::BaseStats;
using game::evolution::EvolutionInputs;
using game::evolution::RequirementReport;
using game::evolution::testing::inputsOf;
using game::evolution::testing::TableFixture;

std::vector<std::string> keysOf(const std::vector<RequirementRow>& rows) {
  std::vector<std::string> keys;
  for (const RequirementRow& row : rows) {
    keys.push_back(row.key);
  }
  return keys;
}

} // namespace

TEST_CASE("requirement ranges: a minimum, a maximum and the weight window") {
  CHECK(requirementRange(100, false) == "100+");
  CHECK(requirementRange(3, true) == "0-3");
  CHECK(requirementRange(0, true) == "0-0");
  // No requirement (-1) reads as any amount.
  CHECK(requirementRange(-1, false) == "0+");
  CHECK(weightRange(25, 35) == "25-35");
  CHECK(weightRange(-6, 4) == "0-4");
}

TEST_CASE("the chart's rows of a requirement come from the scoring's report") {
  TableFixture fixture;
  fixture.setPath("agum", {"species_b"});
  assets::EvolutionRequirementEntry& row = fixture.requirement("species_b");
  row.care = 3;
  row.weight = 30;
  row.hp = 50;
  row.brain = 100;
  row.digimon = "kune";
  row.battles = 10;
  row.flags = game::evolution::CareIsMaximumFlag | game::evolution::BattlesAreMaximumFlag;
  const game::evolution::Evolution evolution = fixture.makeEvolution();
  EvolutionInputs inputs = inputsOf("agum", 20);
  inputs.careMistakes = 1;
  inputs.weight = 40;
  inputs.stats =
      BaseStats{.hp = 530, .mp = 0, .offense = 0, .defense = 0, .speed = 0, .brains = 90};
  inputs.battles = 2;
  const auto report = evolution.report(inputs, {}, TableFixture::index("species_b"));
  REQUIRE(report.has_value());
  const std::vector<RequirementRow> rows = requirementRows(*report);
  CHECK(keysOf(rows) == std::vector<std::string>{"care", "weight", "stats", "hp", "brn", "bonus",
                                                 "from", "battles"});
  CHECK(encodeRanges(rows) ==
        std::format("care:0-3|weight:25-35|stats:min|hp:500+|brn:100+|bonus:|from:{}|battles:0-10",
                    TableFixture::index("kune")));
  // HP shows as the stat itself, its minimum times the divisor.
  CHECK(rows.at(3).value == "530");
  CHECK(rows.at(3).met);
  CHECK_FALSE(rows.at(4).met);
  CHECK_FALSE(rows.at(1).met);
  // Care and the battles bonus are met: 2 of 4, one short of passing.
  CHECK(encodeValues(*report, rows) ==
        std::format("2:3:4|1:1|40:0|:0|530:1|90:0|:1|{}:0|2:1", TableFixture::index("agum")));
}

TEST_CASE("a rookie target names the partner's highest stat; a special one is a single row") {
  TableFixture fixture;
  fixture.requirement("agum").speed = game::evolution::FavouredStat;
  fixture.requirement("agum").care = 0;
  fixture.requirement("agum").weight = 10;
  const game::evolution::Evolution evolution = fixture.makeEvolution();
  EvolutionInputs inputs = inputsOf("koro", 24);
  inputs.stats.speed = 40;
  const auto rookie = evolution.report(inputs, {}, TableFixture::index("agum"));
  REQUIRE(rookie.has_value());
  const std::vector<RequirementRow> rows = requirementRows(*rookie);
  CHECK(keysOf(rows) == std::vector<std::string>{"care", "weight", "stats", "spd"});
  CHECK(rows.at(2).range == "top");
  CHECK(rows.at(2).value == "spd");
  CHECK(rows.at(3).met);

  // The fixture's other forms ask for nothing: no care and no weight, a special evolution.
  const auto special = evolution.report(inputs, {}, TableFixture::index("nume"));
  REQUIRE(special.has_value());
  CHECK(keysOf(requirementRows(*special)) == std::vector<std::string>{"special"});
}

TEST_CASE("a ruleset's gate is the first row, met or not") {
  TableFixture fixture;
  fixture.requirement("panj").care = 2;
  fixture.requirement("panj").weight = 25;
  game::evolution::EvolutionTables tables = fixture.build();
  dwe::ruleset::SpeciesExtra extra;
  extra.species["panj"] =
      dwe::ruleset::SpeciesPatch{.gate = dwe::ruleset::SpeciesGate{.triggers = {248}}};
  REQUIRE(tables.useExtra(extra).has_value());
  auto evolution = game::evolution::Evolution::create(
      game::evolution::testing::originalRules(), std::move(tables),
      std::make_unique<game::evolution::OriginalSelection>(),
      std::make_unique<game::evolution::OriginalScoring>());
  REQUIRE(evolution.has_value());
  EvolutionInputs inputs = inputsOf("nume", 0);
  inputs.trigger = [](std::int32_t trigger) { return trigger == 248; };
  const auto open = evolution->report(inputs, {}, TableFixture::index("panj"));
  REQUIRE(open.has_value());
  const std::vector<RequirementRow> rows = requirementRows(open.value());
  CHECK(keysOf(rows) == std::vector<std::string>{"gate", "care", "weight", "stats"});
  CHECK(rows.front().met);
  inputs.trigger = {};
  const auto shut = evolution->report(inputs, {}, TableFixture::index("panj"));
  REQUIRE(shut.has_value());
  CHECK_FALSE(requirementRows(shut.value()).front().met);
  CHECK(shut.value().points == 0);
}

TEST_CASE("the partner's line of the chart") {
  const DiscoveredForms discovered{3, 8, 11};
  game::evolution::EvolutionOutlook outlook;
  outlook.species = 3;
  outlook.evolutionTimer = 17;
  outlook.checkHours = 72;
  outlook.next = 8;
  CHECK(encodeOutlook(outlook, discovered) == "3,17,72,0,8,-1,-1");
  outlook.evolutionTimer = 96;
  outlook.next.reset();
  outlook.fallback = 11;
  outlook.fallbackHours = 96;
  CHECK(encodeOutlook(outlook, discovered) == "3,96,72,1,-1,11,96");
}

TEST_CASE("the chart names no form the player has not discovered") {
  CHECK(parseDiscovered("3,8,11") == DiscoveredForms{3, 8, 11});
  CHECK(parseDiscovered("").empty());
  CHECK(parseDiscovered("4,,x,-1,9") == DiscoveredForms{4, 9});

  game::evolution::EvolutionOutlook outlook;
  outlook.species = 3;
  outlook.evolutionTimer = 96;
  outlook.checkHours = 72;
  outlook.next = 8;
  outlook.fallback = 11;
  outlook.fallbackHours = 96;
  for (const game::evolution::Species target : {8, 9, 10}) {
    RequirementReport report;
    report.target = target;
    outlook.targets.push_back(report);
  }
  // The next form and the fallback not discovered: only "undiscovered", never which one.
  const DiscoveredForms onlyPartner{3, 9};
  CHECK(encodeOutlook(outlook, onlyPartner) == "3,96,72,1,-2,-2,96");
  // Only the discovered targets keep their requirement.
  const auto shown = shownTargets(outlook, onlyPartner);
  REQUIRE(shown.size() == 1);
  CHECK(shown.front()->target == 9);
  CHECK(shownTargets(outlook, {}).empty());
  CHECK(shownTargets(outlook, DiscoveredForms{8, 9, 10}).size() == 3);
}

} // namespace dwe::app
