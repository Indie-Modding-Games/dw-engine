#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/game_systems.hpp>
#include <dwe/session/gym_adapter.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
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

TEST_CASE("the game pack's gym rules read, and the second battlefield holds every punch") {
  // gym.yaml's betl03 offense swap sets hold_each_punch (trn2_off.c sets animFlag 2 on every
  // punch frame); the savanna's (trn_off.c, instance 6) does not.
  const TestPacks packs;
  auto gym = std::make_unique<dwe::session::GymAdapter>();
  const dwe::session::GymAdapter& rules = *gym;
  std::vector<std::unique_ptr<dwe::session::SessionSystem>> systems;
  systems.push_back(std::move(gym));
  const auto session = newGameWith(packs, std::move(systems));
  const auto holdsEachPunch = [&rules](std::string_view map) -> std::optional<bool> {
    for (const dwe::game::gym::CourseRules& course : rules.rules().courses) {
      const auto* offense = std::get_if<dwe::game::gym::OffenseCourse>(&course.detail);
      if (course.map == map && offense != nullptr && offense->swap) {
        return offense->swap->holdEachPunch;
      }
    }
    return std::nullopt;
  };
  CHECK(holdsEachPunch("betl03") == std::optional{true});
  CHECK(holdsEachPunch("gias09") == std::optional{false});
}
