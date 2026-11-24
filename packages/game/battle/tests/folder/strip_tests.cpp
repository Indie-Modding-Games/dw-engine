#include <dwe/game/battle/folder/formulas.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "folder_kit.hpp"

using namespace folder_test;

TEST_CASE("a folder battle's strip gives orders and stances") {
  Battle battle = startBattle(folderDuel(evenStats(300, 100), evenStats(300, 100)), 1);
  CHECK(battle.system() == BattleSystemId::Folder);
  REQUIRE(battle.folderOf(PartnerId) != nullptr);
  CHECK(battle.defenseBarCapacity(PartnerId) == 600);
  const CommandState& commands = battle.state().commands;
  CHECK(commands.menu == std::vector<Command>{Command::Finisher, Command::Flee, Command::Change,
                                              Command::Defend, Command::Distance, Command::Moderate,
                                              Command::Attack, Command::Technique2,
                                              Command::Technique1, Command::YourCall});
  CHECK(commands.current == Command::Attack);
  REQUIRE(battle.command(Command::Defend));
  CHECK(battle.folderOf(PartnerId)->stance == Stance::Guard);
  CHECK(battle.state().commands.current == Command::Defend);
  REQUIRE(battle.command(Command::Technique2));
  REQUIRE(battle.folderOf(PartnerId)->queue.size() == 1);
  CHECK(battle.folderOf(PartnerId)->queue[0].slot == 1);
  REQUIRE(battle.command(Command::YourCall));
  CHECK(battle.folderOf(PartnerId)->queue.empty());
  CHECK_FALSE(battle.command(Command::Finisher));
  CHECK(battle.order(0, 1));
  CHECK_FALSE(battle.order(2, 1));
  CHECK_FALSE(battle.order(0, PartnerId + 7));
  CHECK(battle.command(Command::Flee));
}

TEST_CASE("each strip order keeps the foe Change picked when it was given") {
  BattleSetup setup = folderDuel(evenStats(300, 100), evenStats(300, 100));
  setup.enemies.push_back(fighterWith("foe2", evenStats(300, 100), {"punch"}, {.x = 600, .z = 0}));
  Battle battle = startBattle(std::move(setup), 1);
  REQUIRE(battle.state().fighters[PartnerId].target == FighterId{1});
  REQUIRE(battle.command(Command::Change));
  REQUIRE(battle.state().fighters[PartnerId].target == FighterId{2});
  REQUIRE(battle.command(Command::Technique1));
  REQUIRE(battle.command(Command::Change));
  REQUIRE(battle.command(Command::Technique2));
  const std::vector<Order>& queue = battle.folderOf(PartnerId)->queue;
  REQUIRE(queue.size() == 2);
  CHECK(queue[0].target == FighterId{2});
  CHECK(queue[1].target == FighterId{1});
}
