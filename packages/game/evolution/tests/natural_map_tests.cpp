#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/strategies.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>
#include <utility>
#include <vector>

#include "fixture.hpp"

namespace dwe::game::evolution {

using testing::inputsOf;
using testing::TableFixture;
using testing::valueOf;

namespace {

constexpr std::uint64_t Seed = 7;

NaturalCheck checkAt(const Evolution& evolution, const EvolutionInputs& inputs) {
  ruleset::Random random{Seed};
  return evolution.checkNatural(inputs, {}, CheckSpan::hourly(1), random);
}

} // namespace

TEST_CASE("a mapped natural evolution gives the mapped form of the same stage, from the chart's "
          "pick") {
  Evolution evolution = TableFixture{}.makeEvolution();
  std::vector<std::pair<Species, Species>> asked;
  evolution.mapNaturalTargets([&](Species from, Species target) {
    asked.emplace_back(from, target);
    return target == TableFixture::index("koro") ? TableFixture::index("tane") : target;
  });
  const NaturalCheck check = checkAt(evolution, inputsOf("bota", 6));
  REQUIRE(check.evolution);
  CHECK(valueOf(check.evolution).target == TableFixture::index("tane"));
  CHECK(valueOf(check.evolution).reason == EvolutionReason::Normal);
  CHECK(evolution.tables().levelOf(TableFixture::index("tane")) ==
        evolution.tables().levelOf(TableFixture::index("koro")));
  REQUIRE_FALSE(asked.empty());
  CHECK(asked.front() == std::pair{TableFixture::index("bota"), TableFixture::index("koro")});
}

TEST_CASE("the rookie's fallback is mapped too, and the outlook shows the mapped forms") {
  Evolution evolution = TableFixture{}.makeEvolution();
  evolution.mapNaturalTargets([](Species /*from*/, Species target) {
    return target == TableFixture::index("nume") ? TableFixture::index("aird") : target;
  });
  const NaturalCheck check = checkAt(evolution, inputsOf("agum", 96));
  REQUIRE(check.evolution);
  CHECK(valueOf(check.evolution).target == TableFixture::index("aird"));
  const EvolutionOutlook outlook = evolution.outlook(inputsOf("agum", 96), {});
  CHECK(outlook.fallback == TableFixture::index("aird"));
}

TEST_CASE("without a map, or after it is cleared, the chart's forms stay") {
  Evolution evolution = TableFixture{}.makeEvolution();
  evolution.mapNaturalTargets(
      [](Species /*from*/, Species /*target*/) { return TableFixture::index("tane"); });
  evolution.mapNaturalTargets({});
  const NaturalCheck check = checkAt(evolution, inputsOf("bota", 6));
  REQUIRE(check.evolution);
  CHECK(valueOf(check.evolution).target == TableFixture::index("koro"));
}

} // namespace dwe::game::evolution
