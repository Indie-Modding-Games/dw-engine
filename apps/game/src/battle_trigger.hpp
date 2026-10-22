#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/battle_adapter.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace dwe::app {

// The name of the console's and the scripts' battle command: battle("") or battle("2,3").
inline constexpr std::string_view BattleCommand = "battle";

// The Digimon table of the ruleset, for the battle's enemies and the names the screens show.
core::Result<assets::DigimonTable> speciesTable(const assets::VirtualFileSystem& files,
                                                const ruleset::Ruleset& rules);

// The NPCs a battle command names: a comma-separated list of the map's NPC numbers, or none for the
// first NPC that can fight.
core::Result<std::vector<std::size_t>> npcsOf(std::string_view argument);

// A battle against NPCs of the field's map, as the original's scripts start one (script_interp.c:
// SCRIPT_OP_START_BATTLE, btl.c: the enemies are NPCs of the map): each enemy is its NPC record on
// the Digimon table (game::battle::enemyFromMap), the tamer where the player stands. Without NPC
// numbers, the first NPC with a Digimon.
//
// This is the engine's debug trigger until the map scripts run (phase 4): it changes no rule of the
// battle.
core::Result<session::BattleRequest> mapBattleRequest(const game::field::Field& field,
                                                      const assets::MapDocument& map,
                                                      const assets::DigimonTable& species,
                                                      const std::vector<std::size_t>& npcs);

} // namespace dwe::app
