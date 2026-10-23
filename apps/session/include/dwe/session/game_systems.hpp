#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/evolution/tables.hpp>
#include <dwe/session/session_system.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::ruleset {
class Ruleset;
} // namespace dwe::ruleset

namespace dwe::session {

class BattleAdapter;
class EvolutionAdapter;
class InventoryAdapter;
class ShopCounter;
class StoryAdapter;
class StoryGameOrders;
class StoryScenes;

// The tables the game systems read. Loaded once, they are shared by many sessions (dwe-sim's runs);
// a table left empty is read by its adapter from the packs.
struct SystemTables {
  std::optional<game::evolution::EvolutionTables> evolution;
  std::optional<assets::ItemTable> items;
  std::optional<assets::ItemEffectTable> itemEffects;
  std::optional<assets::DigimonTable> digimon;
  std::optional<assets::MoveTable> moves;
};

// Reads every table of the game systems from the packs, as the ruleset names them.
core::Result<SystemTables> loadSystemTables(const assets::VirtualFileSystem& files,
                                            const ruleset::Ruleset& rules);

// The game's systems as adapters for SessionDesc::systems, in the order they tick (evolution,
// inventory, battle, the story's game orders, story scenes, story, gym, shop), and the adapters by
// type for the commands of the game and its menus. The session owns the adapters once given them:
// the typed pointers are valid while it lives.
struct GameSystems {
  std::vector<std::unique_ptr<SessionSystem>> systems;
  EvolutionAdapter* evolution = nullptr;
  InventoryAdapter* inventory = nullptr;
  BattleAdapter* battle = nullptr;
  StoryAdapter* story = nullptr;
  StoryGameOrders* storyOrders = nullptr;
  StoryScenes* scenes = nullptr;
  // The shop the story opens (CALL_ROUTINE 8).
  ShopCounter* shop = nullptr;
};

// The adapters of every game system on the packs, which must outlive them, wired to each other: the
// evolution reads the learned moves from the battle adapter.
GameSystems createGameSystems(const assets::VirtualFileSystem& files, SystemTables tables = {});

} // namespace dwe::session
