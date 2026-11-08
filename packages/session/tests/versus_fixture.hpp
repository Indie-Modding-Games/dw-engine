#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/battle_registry.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/versus_match.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "packages/game/battle/tests/fixture.hpp"

// Made-up species and registered partners for the versus tests of the session and of the game's
// versus screens: no game data.
namespace versus_test {

inline dwe::assets::DigimonRow madeUpRow(std::string id, std::int32_t index,
                                         std::vector<std::string> moves) {
  dwe::assets::DigimonRow row;
  row.id = std::move(id);
  row.index = index;
  row.name = row.id;
  row.radius = 100;
  row.type = 2;
  row.level = 3;
  row.special = {1, std::nullopt, std::nullopt};
  for (std::size_t slot = 0; slot < moves.size(); ++slot) {
    if (!moves[slot].empty()) {
      row.moves.at(slot) = moves[slot];
    }
  }
  return row;
}

// The new game partner the session needs, and two made-up species for the registries.
inline dwe::assets::DigimonTable madeUpSpecies() {
  std::vector<dwe::assets::DigimonRow> rows;
  rows.push_back(madeUpRow("agum", 8, {"punch", "shot", "", "", "big_finish"}));
  rows.push_back(madeUpRow("first_kind", 20, {"punch", "shot", "dynamite_kick", "", "big_finish"}));
  rows.push_back(madeUpRow("second_kind", 21, {"punch", "shot", "", "big_finish"}));
  return dwe::assets::DigimonTable{std::move(rows)};
}

// The adapter's description with the made-up tables.
inline dwe::session::BattleAdapterDesc madeUpTables(const dwe::session::testing::TestPacks& packs) {
  dwe::session::BattleAdapterDesc desc{&packs.files(), madeUpSpecies(),
                                       battle_test::testMoveTable(), nullptr};
  return desc;
}

// One player's machine: its own session and battle adapter over the same packs.
struct Machine {
  explicit Machine(const dwe::session::testing::TestPacks& packs) {
    auto adapter = std::make_unique<dwe::session::BattleAdapter>(madeUpTables(packs));
    battle = adapter.get();
    std::vector<std::unique_ptr<dwe::session::SessionSystem>> systems;
    systems.push_back(std::move(adapter));
    session = dwe::session::testing::newGameWith(packs, std::move(systems));
  }

  std::unique_ptr<dwe::session::GameSession> session;
  dwe::session::BattleAdapter* battle = nullptr;
};

inline dwe::session::RegisteredPartner registered(std::string name, std::string species,
                                                  std::int32_t brains, std::int32_t discipline) {
  return {.name = std::move(name),
          .species = std::move(species),
          .hp = 400,
          .mp = 300,
          .offense = 120,
          .defense = 60,
          .speed = 100,
          .brains = brains,
          .discipline = discipline,
          .techniques = {std::string{"punch"}, std::string{"shot"}, std::nullopt}};
}

} // namespace versus_test
