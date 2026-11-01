#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/battle_adapter.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dwe::app {

// The name of the console's and the scripts' battle command: battle("") or battle("2,3"); with
// this prefix, battle("story:2"), a battle of the story (no fleeing, enemies spend no MP).
inline constexpr std::string_view BattleCommand = "battle";
inline constexpr std::string_view StoryBattlePrefix = "story:";
// The debug command that ends the running fight with the enemies beaten: battle_win("").
inline constexpr std::string_view BattleWinCommand = "battle_win";
// And the one that ends it with the partner beaten: battle_lose("").
inline constexpr std::string_view BattleLoseCommand = "battle_lose";

// The Digimon table of the ruleset, for the battle's enemies and the names the screens show.
core::Result<assets::DigimonTable> speciesTable(const assets::VirtualFileSystem& files,
                                                const ruleset::Ruleset& rules);

// The NPCs a battle command names: a comma-separated list of the map's NPC numbers, or none for the
// first NPC that can fight.
core::Result<std::vector<std::size_t>> npcsOf(std::string_view argument);

// A battle against the map's NPCs, and the script id of the NPC each enemy is, in the enemies'
// order: the battle screen shows each enemy on its NPC.
struct MapBattle {
  session::BattleRequest request;
  std::vector<std::int32_t> enemyNpcs;
};

// A battle against NPCs of the field's map, as the original's scripts start one (script_interp.c:
// SCRIPT_OP_START_BATTLE, btl.c: the enemies are NPCs of the map): each enemy is its NPC record on
// the Digimon table (game::battle::enemyFromMap) where the NPC stands, the tamer where the player
// stands and the partner where it stands, all in world axes: this is where the field's positions
// become the battle's. Without NPC numbers, the first NPC with a Digimon.
//
// This is the engine's debug trigger until the map scripts run (phase 4): it changes no rule of the
// battle.
core::Result<MapBattle> mapBattleRequest(const game::field::Field& field,
                                         const assets::MapDocument& map,
                                         const assets::DigimonTable& species,
                                         const std::vector<std::size_t>& npcs);

} // namespace dwe::app
