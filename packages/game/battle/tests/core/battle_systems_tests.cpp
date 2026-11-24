#include <dwe/game/battle/folder/formulas.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "../folder/folder_kit.hpp"

using namespace folder_test;

TEST_CASE("the battle system names read back") {
  CHECK(battleSystemName(BattleSystemId::Classic) == "classic");
  CHECK(battleSystemName(BattleSystemId::Folder) == "folder");
  CHECK(battleSystemOf("folder") == BattleSystemId::Folder);
  CHECK(battleSystemOf("classic") == BattleSystemId::Classic);
  CHECK_FALSE(battleSystemOf("other").has_value());
}

TEST_CASE("a classic battle takes no orders and keeps no folder state") {
  Battle battle = startBattle(duel(evenStats(300, 100), evenStats(300, 100)), 1);
  CHECK(battle.system() == BattleSystemId::Classic);
  CHECK_FALSE(battle.state().folder.has_value());
  CHECK(battle.folderOf(PartnerId) == nullptr);
  CHECK(battle.defenseBarCapacity(PartnerId) == 0);
  CHECK_FALSE(battle.order(0, std::nullopt));
  CHECK_FALSE(battle.setStance(Stance::Hold));
}

TEST_CASE("a versus match cannot be a folder battle") {
  BattleSetup setup = folderDuel(evenStats(300, 100), evenStats(300, 100));
  setup.versus = VersusSetup{.rivalDiscipline = 40};
  CHECK_FALSE(
      Battle::start(std::move(setup), originalRules(), testCatalog(), originalStrategies(), 1));
}
