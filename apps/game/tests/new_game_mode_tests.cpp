#include <dwe/game/modes/game_mode.hpp>
#include <dwe/game/modes/mode_catalog.hpp>

#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "app_flow.hpp"
#include "new_game_mode.hpp"

using dwe::app::NewGameModes;
using dwe::app::PlayStart;
using dwe::game::modes::GameMode;
using dwe::game::modes::ModeCatalog;

namespace {

GameMode original() {
  return {.id = "original",
          .nameKey = "modes.original_name",
          .descriptionKey = "modes.original_description",
          .ruleset = "original",
          .rulesets = {"original", "oculto", "oculto_mega"},
          .order = 0};
}

GameMode arena() {
  return {.id = "arena",
          .nameKey = "my_mod.arena_name",
          .descriptionKey = "my_mod.arena_description",
          .ruleset = "arena_rules",
          .rulesets = {"arena_rules"},
          .order = 10};
}

NewGameModes modes(std::vector<GameMode> list) {
  return NewGameModes{ModeCatalog{std::move(list)}};
}

} // namespace

TEST_CASE("a new game asks its mode only when the packs have more than one") {
  CHECK_FALSE(modes({original()}).asks());
  CHECK(modes({original(), arena()}).asks());
}

TEST_CASE("the original's mode goes on to the rules screen, which picks its ruleset") {
  PlayStart start;
  CHECK(modes({original(), arena()}).choose("original", start) == NewGameModes::Next::AskRules);
  CHECK(start.modeId == "original");
  CHECK_FALSE(start.rulesetId.has_value());
}

TEST_CASE("a mode without the hidden content starts with its own ruleset") {
  PlayStart start;
  CHECK(modes({original(), arena()}).choose("arena", start) == NewGameModes::Next::Start);
  CHECK(start.modeId == "arena");
  CHECK(start.rulesetId == "arena_rules");
}

TEST_CASE("an unknown mode changes nothing") {
  PlayStart start;
  CHECK(modes({original()}).choose("missing", start) == NewGameModes::Next::Unknown);
  CHECK(start == PlayStart{});
}

TEST_CASE("the mode list is published in the catalog's order, at most six") {
  std::vector<GameMode> list{original(), arena()};
  for (int index = 0; index < 6; ++index) {
    GameMode extra = arena();
    extra.id = "extra" + std::to_string(index);
    extra.order = 20 + index;
    list.push_back(std::move(extra));
  }
  const auto settings = modes(std::move(list)).settings();
  REQUIRE(!settings.empty());
  CHECK(settings.front().name == "new_game_mode.count");
  CHECK(std::get<double>(settings.front().value) == 6.0);
  CHECK(settings.size() == 1 + (6 * 3));
  CHECK(settings[1].name == "new_game_mode.0.id");
  CHECK(std::get<std::string>(settings[1].value) == "original");
  CHECK(std::get<std::string>(settings[4].value) == "arena");
  CHECK(settings[5].name == "new_game_mode.1.name");
  CHECK(std::get<std::string>(settings[5].value) == "my_mod.arena_name");
}
