#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/strategies.hpp>
#include <dwe/ruleset/species_extra.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "fixture.hpp"

// Every species as a partner (docs/modulos/game/evoluciones-extra.md): the all_species targets are
// fallback ones, picked only when none of the original's passes.
namespace dwe::game::evolution {

namespace {

using testing::TableFixture;
using testing::valueOf;

constexpr std::uint64_t Seed = 5;
// A requirement no fixture partner meets: with two of the four groups failed a target does not
// pass.
constexpr std::int32_t FarWeight = 99;
constexpr std::int32_t HighOffense = 500;
// A rookie's requirement marks its favoured stat with 1.
constexpr std::int32_t FavouredMark = 1;

Evolution evolutionWith(const TableFixture& fixture, const ruleset::SpeciesExtra& extra) {
  EvolutionTables tables = fixture.build();
  const auto used = tables.useExtra(extra);
  REQUIRE_MESSAGE(used.has_value(), testing::errorOf(used));
  auto built =
      Evolution::create(testing::originalRules(), std::move(tables),
                        std::make_unique<OriginalSelection>(), std::make_unique<OriginalScoring>());
  REQUIRE_MESSAGE(built.has_value(), testing::errorOf(built));
  return std::move(*built);
}

ruleset::SpeciesExtra fallback(std::string_view id) {
  ruleset::SpeciesExtra extra;
  extra.species[std::string{id}] = ruleset::SpeciesPatch{.yields = true};
  return extra;
}

// What the periodic check would pick once due.
std::optional<Species> nextOf(const Evolution& evolution, const EvolutionInputs& inputs) {
  const EvolutionOutlook outlook = evolution.outlook(inputs, {});
  return outlook.checkHours ? outlook.next : std::nullopt;
}

EvolutionInputs championInputs() {
  EvolutionInputs inputs = testing::inputsOf("nume", 1000);
  inputs.stats.offense = 10;
  return inputs;
}

// A champion whose two targets both pass, the fallback one first in its path.
TableFixture championFixture() {
  TableFixture fixture;
  fixture.setPath("nume", {"bird", "mono"});
  fixture.requirement("bird").offense = 0;
  fixture.requirement("mono").offense = 0;
  return fixture;
}

} // namespace

TEST_CASE("a fallback target is picked only when no target of the original's passes") {
  TableFixture fixture = championFixture();
  // Without fallback targets the pick is the original's: on a tie, the first.
  CHECK(nextOf(evolutionWith(fixture, {}), championInputs()) == TableFixture::index("bird"));
  CHECK(nextOf(evolutionWith(fixture, fallback("bird")), championInputs()) ==
        TableFixture::index("mono"));
  // The original's target fails two groups: the fallback one is picked.
  fixture.requirement("mono").offense = HighOffense;
  fixture.requirement("mono").weight = FarWeight;
  CHECK(nextOf(evolutionWith(fixture, fallback("bird")), championInputs()) ==
        TableFixture::index("bird"));
}

TEST_CASE("an original's target that passes wins over a fallback one with more points") {
  TableFixture fixture;
  fixture.setPath("koro", {"species_c", "agum"});
  // With every stat at 0 the last one, brains, is the highest: both get the stats group. agum
  // passes with three points, species_c with four (it also names koro).
  fixture.requirement("agum").brain = FavouredMark;
  fixture.requirement("species_c").brain = FavouredMark;
  fixture.requirement("species_c").digimon = "koro";
  const EvolutionInputs inputs = testing::inputsOf("koro", 1000);
  CHECK(nextOf(evolutionWith(fixture, {}), inputs) == TableFixture::index("species_c"));
  CHECK(nextOf(evolutionWith(fixture, fallback("species_c")), inputs) ==
        TableFixture::index("agum"));
}

TEST_CASE("a fallback form that learns no technique fights with the first move of its list") {
  TableFixture fixture = championFixture();
  fixture.requirement("mono").offense = HighOffense;
  fixture.requirement("mono").weight = FarWeight;
  // No specialty: the original's rule teaches it nothing.
  fixture.form("bird").special = {std::nullopt, std::nullopt, std::nullopt};
  const Evolution evolution = evolutionWith(fixture, fallback("bird"));
  EvolutionInputs inputs = championInputs();
  inputs.evolutionTimer = valueOf(evolution.outlook(inputs, {}).checkHours);
  ruleset::Random random{Seed};
  const NaturalCheck check = evolution.checkNatural(inputs, {}, CheckSpan::hourly(1), random);
  REQUIRE(check.evolution);
  CHECK(valueOf(check.evolution).target == TableFixture::index("bird"));
  CHECK(valueOf(check.evolution).learnedMove == std::optional<std::string>{"move_2"});
  CHECK(valueOf(check.evolution).equippedMoves.front() == std::optional<std::int32_t>{0});
}

} // namespace dwe::game::evolution
