#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/game_systems.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "session_fixture.hpp"

using dwe::session::testing::errorOf;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// Made-up cards in the shape dwx extracts.
constexpr std::string_view CardTable = R"(prices: [5000, 1500]
cards:
- {index: 0, digimon: null, sprite_id: 0, merit_value: 100}
- {index: 1, digimon: null, sprite_id: 1, merit_value: 50}
)";

} // namespace

TEST_CASE("the game's systems read the cards from the packs when they are given no tables") {
  const TestPacks packs{{"data/cards.yaml", CardTable}};
  dwe::session::GameSystems systems = dwe::session::createGameSystems(packs.files());
  REQUIRE(systems.cards != nullptr);
  dwe::session::CardsAdapter& cards = *systems.cards;
  // Only the cards play: the stand-in packs have no other tables.
  std::vector<std::unique_ptr<dwe::session::SessionSystem>> playing;
  for (auto& system : systems.systems) {
    if (system.get() == static_cast<dwe::session::SessionSystem*>(systems.cards)) {
      playing.push_back(std::move(system));
    }
  }
  const auto session = newGameWith(packs, std::move(playing));
  // Without the table the player never holds a card and a script's give_card changes nothing.
  const auto given = cards.giveCopy(1);
  REQUIRE_MESSAGE(given.has_value(), errorOf(given));
  CHECK(cards.copies(1) == 1);
  CHECK(cards.copies(0) == 0);
}
