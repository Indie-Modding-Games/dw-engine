#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/strategies.hpp>
#include <dwe/ruleset/species_extra.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <set>

#include "fixture.hpp"

// The species patches' rules (docs/modulos/game/evolution.md, «Contenido oculto»): gates before a
// target scores, and the optional Ultimate to Mega step.
namespace dwe::game::evolution {

namespace {

using testing::TableFixture;
using testing::valueOf;

constexpr std::uint64_t Seed = 11;

// The inputs of a form, with the one stat the targets ask for (the champion's pick averages it).
EvolutionInputs inputsOf(std::string_view id, std::int32_t timer) {
  EvolutionInputs inputs = testing::inputsOf(id, timer);
  inputs.stats.offense = 10;
  return inputs;
}

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

NaturalCheck checkAt(const Evolution& evolution, const EvolutionInputs& inputs) {
  ruleset::Random random{Seed};
  return evolution.checkNatural(inputs, {}, CheckSpan::hourly(1), random);
}

// What the periodic check would pick once due (the outlook's pick), nothing when the level never
// checks.
std::optional<Species> nextOf(const Evolution& evolution, const EvolutionInputs& inputs) {
  const EvolutionOutlook outlook = evolution.outlook(inputs, {});
  return outlook.checkHours ? outlook.next : std::nullopt;
}

// A champion that can only become panj, which asks for nothing but its gate.
TableFixture gatedFixture() {
  TableFixture fixture;
  fixture.setPath("nume", {"panj"});
  fixture.requirement("panj").offense = 0;
  return fixture;
}

} // namespace

TEST_CASE("a gated target scores only once its triggers are set, at its hours") {
  const TableFixture fixture = gatedFixture();
  ruleset::SpeciesExtra extra;
  extra.species["panj"] = ruleset::SpeciesPatch{
      .gate = ruleset::SpeciesGate{.triggers = {248},
                                   .hours = ruleset::HourRange{.from = 20, .to = 6}}};
  const Evolution evolution = evolutionWith(fixture, extra);
  EvolutionInputs inputs = inputsOf("nume", 1000);
  // No story state given: the gate is shut.
  CHECK_FALSE(nextOf(evolution, inputs));
  std::set<std::int32_t> set;
  inputs.trigger = [&set](std::int32_t trigger) { return set.contains(trigger); };
  inputs.hourOfDay = 22;
  CHECK_FALSE(nextOf(evolution, inputs));
  set.insert(248);
  CHECK(nextOf(evolution, inputs) == TableFixture::index("panj"));
  inputs.hourOfDay = 12;
  CHECK_FALSE(nextOf(evolution, inputs));
  const auto report = evolution.report(inputs, {}, TableFixture::index("panj"));
  REQUIRE(report);
  CHECK(valueOf(report->gateMet) == false);
  CHECK(report->points == 0);
}

TEST_CASE("a cup gate is open without the tournaments and shut until they record the cup won") {
  const TableFixture fixture = gatedFixture();
  ruleset::SpeciesExtra extra;
  extra.species["panj"] = ruleset::SpeciesPatch{.gate = ruleset::SpeciesGate{.cup = 16}};
  const Evolution evolution = evolutionWith(fixture, extra);
  EvolutionInputs inputs = inputsOf("nume", 1000);
  CHECK(nextOf(evolution, inputs) == TableFixture::index("panj"));
  inputs.cupWon = [](std::int32_t) { return false; };
  CHECK_FALSE(nextOf(evolution, inputs));
  inputs.cupWon = [](std::int32_t cup) { return cup == 16; };
  CHECK(nextOf(evolution, inputs) == TableFixture::index("panj"));
}

TEST_CASE("without the Mega step an ultimate never checks; with it, it checks from its hours") {
  TableFixture fixture;
  fixture.setPath("veda", {"mtet"});
  // The champion's pick averages the stats a target asks for, so each target asks for one.
  fixture.requirement("mtet").offense = 0;
  fixture.requirement("veda").offense = 0;
  const Evolution plain = evolutionWith(fixture, {});
  CHECK_FALSE(nextOf(plain, inputsOf("veda", 1000)));

  ruleset::SpeciesExtra extra;
  extra.megaStep = ruleset::MegaStep{
      .checkHours = 120, .lifetimeHours = 72, .tamerLevelChance = 100, .mega = {"mtet"}};
  const Evolution stepped = evolutionWith(fixture, extra);
  CHECK(stepped.outlook(inputsOf("veda", 0), {}).checkHours == 120);
  CHECK_FALSE(stepped.outlook(inputsOf("mtet", 0), {}).checkHours);
  CHECK_FALSE(checkAt(stepped, inputsOf("veda", 119)).evolution);
  CHECK(nextOf(stepped, inputsOf("veda", 120)) == TableFixture::index("mtet"));
  // A Mega is the last step.
  fixture.setPath("mtet", {"veda"});
  const Evolution again = evolutionWith(fixture, extra);
  CHECK_FALSE(nextOf(again, inputsOf("mtet", 1000)));

  // Becoming a Mega restarts the counter and gives the step's life bonus.
  const NaturalCheck check = checkAt(stepped, inputsOf("veda", 120));
  REQUIRE(check.evolution);
  CHECK(valueOf(check.evolution).evolutionTimer == 0);
  CHECK(valueOf(check.evolution).lifetimeBonusHours == 72);
}

} // namespace dwe::game::evolution
