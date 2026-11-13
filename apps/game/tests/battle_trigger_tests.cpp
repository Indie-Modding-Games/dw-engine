#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/field/npcs.hpp>
#include <dwe/scene/world.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "battle_trigger.hpp"
#include "packages/game/field/tests/fixture.hpp"

namespace {

using dwe::assets::MapDocument;
using dwe::game::field::Field;

constexpr std::int32_t SharedScriptId = 5;

std::size_t addFighter(MapDocument& map, std::int32_t x, std::int32_t z) {
  dwe::assets::MapNpc npc;
  npc.typeId = 1;
  npc.digimon = "foe";
  // The record is in the original's axes: its z is the world's -z.
  npc.position = {x, 0, -z};
  npc.scriptId = SharedScriptId;
  npc.hp = 1000;
  npc.mp = 100;
  map.npcs.push_back(npc);
  return map.npcs.size() - 1;
}

dwe::assets::DigimonTable foeTable() {
  dwe::assets::DigimonTable table;
  dwe::assets::DigimonRow foe;
  foe.id = "foe";
  foe.index = 1;
  foe.name = "Foe";
  foe.radius = 100;
  foe.height = 300;
  table.digimon.push_back(foe);
  return table;
}

} // namespace

TEST_CASE("each enemy of NPCs that share a script id fights from where its own NPC stands") {
  // btl.c: handleBattleStart puts entities in COMBAT_DATA.player.entityIds; gcan08_1's three wild
  // NPCs of one species share script id 5 and fight from their own places.
  dwe::scene::World world;
  Field field{world, dwe::game::field::testing::originalRules()};
  MapDocument map = dwe::game::field::testing::roomMap("room");
  const std::vector<std::size_t> slots{addFighter(map, 300, 0), addFighter(map, 900, 600),
                                       addFighter(map, 900, -600)};
  field.enterMapAt(map, {.position = {0.0F, 0.0F, 0.0F}, .yaw = 0.0F});
  for (const std::size_t slot : slots) {
    REQUIRE(field.npcs().spawn(
        {.slot = slot, .species = 1, .autotalk = false, .model = "npc", .radius = 100.0F}));
  }
  const auto battle = dwe::app::mapBattleRequest(field, map, foeTable(), slots);
  REQUIRE(battle.has_value());
  REQUIRE(battle->request.enemies.size() == 3);
  REQUIRE(battle->enemyNpcs.size() == 3);
  for (std::size_t index = 0; index < slots.size(); ++index) {
    CHECK(battle->enemyNpcs[index] == static_cast<std::int32_t>(slots[index]));
    const glm::vec3 at = *field.npcs().positionAt(slots[index]);
    CHECK(battle->request.enemies[index].position.x == static_cast<std::int32_t>(at.x));
    CHECK(battle->request.enemies[index].position.z == static_cast<std::int32_t>(at.z));
  }
  CHECK(battle->request.enemies[0].position.x != battle->request.enemies[1].position.x);
  CHECK(battle->request.enemies[1].position.z != battle->request.enemies[2].position.z);
}

TEST_CASE("a story battle under trigger 1 cannot be fled, whatever started it") {
  // btl.c: handleBattleStart: FLEE_DISABLED[0] = isTriggerSet(1); battle_tick.c: handleCommands
  // skips Run then. Fleeing such a fight skipped the rest of its section, which clears trigger 1
  // and the predefined battle's variable 250, so the next story battle found stale enemies.
  dwe::scene::World world;
  Field field{world, dwe::game::field::testing::originalRules()};
  MapDocument map = dwe::game::field::testing::roomMap("room");
  const std::vector<std::size_t> slots{addFighter(map, 300, 0)};
  field.enterMapAt(map, {.position = {0.0F, 0.0F, 0.0F}, .yaw = 0.0F});
  REQUIRE(field.npcs().spawn(
      {.slot = slots[0], .species = 1, .autotalk = false, .model = "npc", .radius = 100.0F}));
  auto battle = dwe::app::mapBattleRequest(field, map, foeTable(), slots);
  REQUIRE(battle.has_value());
  CHECK_FALSE(battle->request.scripted);

  dwe::session::BattleRequest free = battle->request;
  dwe::app::applyFixedFight(free, false);
  CHECK_FALSE(free.scripted);

  dwe::session::BattleRequest fixed = battle->request;
  dwe::app::applyFixedFight(fixed, true);
  CHECK(fixed.scripted);

  // A request that is already a story battle (the console's battle("story:...")) stays one.
  dwe::session::BattleRequest story = battle->request;
  story.scripted = true;
  dwe::app::applyFixedFight(story, false);
  CHECK(story.scripted);
}
