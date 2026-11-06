#pragma once

#include <dwe/game/partner/species.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/session_system.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "packages/game/evolution/tests/fixture.hpp"
#include "session_fixture.hpp"

namespace dwe::session::testing {

// The partner species of every form of the evolution tests' made-up tables, so the partner can
// become any of them.
inline std::shared_ptr<const game::partner::SpeciesTable> fixtureFormsTable() {
  game::partner::SpeciesTable table;
  for (const game::evolution::testing::FixtureForm& form : game::evolution::testing::FixtureForms) {
    table.add(madeUpSpecies(std::string{form.id}, static_cast<game::partner::Level>(form.level)));
  }
  return std::make_shared<const game::partner::SpeciesTable>(std::move(table));
}

// A started session on the test packs with the fixture forms and the given systems.
inline std::unique_ptr<GameSession> newGameWith(const TestPacks& packs,
                                                std::vector<std::unique_ptr<SessionSystem>> systems,
                                                game::save::SaveStorage* storage = nullptr,
                                                std::uint64_t seed = 7,
                                                std::string_view rulesetId = "original") {
  auto session = GameSession::create({.files = &packs.files(),
                                      .rulesetId = std::string{rulesetId},
                                      .rules = std::nullopt,
                                      .species = fixtureFormsTable(),
                                      .seed = seed,
                                      .storage = storage,
                                      .gameVersion = "test",
                                      .wallClock = [] { return SaveDate; },
                                      .systems = std::move(systems)});
  REQUIRE_MESSAGE(session.has_value(), errorOf(session));
  auto started = (*session)->startNewGame();
  REQUIRE_MESSAGE(started.has_value(), errorOf(started));
  return std::move(*session);
}

// The evolution adapter on the made-up tables of `fixture`.
inline std::unique_ptr<EvolutionAdapter>
evolutionAdapter(const TestPacks& packs, const game::evolution::testing::TableFixture& fixture) {
  return std::make_unique<EvolutionAdapter>(
      EvolutionAdapterDesc{.files = &packs.files(), .tables = fixture.build()});
}

} // namespace dwe::session::testing
