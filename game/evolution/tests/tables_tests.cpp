// Which Digimon each row of the evolution table belongs to once the packs' tables are composed
// (docs/architecture/vfs.md#tablas-por-entradas): the one it names, else its place in the original.
#include <dwe/game/evolution/tables.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

#include "fixture.hpp"

using dwe::game::evolution::EvolutionTables;
using dwe::game::evolution::testing::FixtureForms;
using dwe::game::evolution::testing::TableFixture;

namespace {

// Whether any form's requirement has this weight.
bool anyRequirementWeighs(const EvolutionTables& tables, std::int32_t weight) {
  return std::ranges::any_of(FixtureForms, [&](const auto& form) {
    const auto* requirement = tables.requirement(TableFixture::index(form.id));
    return requirement != nullptr && requirement->weight == weight;
  });
}

} // namespace

TEST_CASE(
    "an evolution row a pack adds belongs to the Digimon it names, with no place of its own") {
  TableFixture fixture;
  fixture.requirement("nume").index = std::nullopt;
  fixture.requirement("nume").weight = 11;
  const EvolutionTables tables = fixture.build();
  REQUIRE(tables.requirement(TableFixture::index("nume")) != nullptr);
  CHECK(tables.requirement(TableFixture::index("nume"))->weight == 11);
}

TEST_CASE("the Digimon a row names wins over its place, which another row may have") {
  TableFixture fixture;
  // A higher pack's row for species_b that carries species_d's place: it is species_b's, and
  // species_d keeps none of it.
  fixture.requirement("species_b").index = TableFixture::index("species_d");
  fixture.requirement("species_b").weight = 22;
  fixture.requirement("species_d").target = std::nullopt;
  fixture.requirement("species_d").index = std::nullopt;
  const EvolutionTables tables = fixture.build();
  CHECK(tables.requirement(TableFixture::index("species_b"))->weight == 22);
  const auto* speciesD = tables.requirement(TableFixture::index("species_d"));
  CHECK((speciesD == nullptr || speciesD->weight != 22));
}

TEST_CASE(
    "a row that names no Digimon belongs to its place in the original, and without one to none") {
  TableFixture fixture;
  fixture.requirement("aird").target = std::nullopt;
  fixture.requirement("aird").weight = 33;
  fixture.requirement("bird").target = std::nullopt;
  fixture.requirement("bird").index = std::nullopt;
  fixture.requirement("bird").weight = 44;
  const EvolutionTables tables = fixture.build();
  CHECK(tables.requirement(TableFixture::index("aird"))->weight == 33);
  CHECK_FALSE(anyRequirementWeighs(tables, 44));
}
