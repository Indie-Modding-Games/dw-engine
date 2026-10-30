#pragma once

#include <dwe/assets/card_table.hpp>
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/fishing_table.hpp>
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
class CalendarAdapter;
class CityAdapter;
class MedalsAdapter;
class FieldItemsAdapter;
class CardsAdapter;
class EvolutionAdapter;
class FishingAdapter;
class InventoryAdapter;
class PoopAdapter;
class ShopCounter;
class ItemKeeper;
class DiscardMenu;
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
  // data/cards, when a pack has it.
  std::optional<assets::CardTable> cards;
  // data/fishing, when a pack has it.
  std::optional<assets::FishingTable> fishing;
};

// Reads every table of the game systems from the packs, as the ruleset names them.
core::Result<SystemTables> loadSystemTables(const assets::VirtualFileSystem& files,
                                            const ruleset::Ruleset& rules);

// The game's systems as adapters for SessionDesc::systems, in the order they tick (evolution,
// inventory, battle, the story's game orders, story scenes, story, gym, shop, item keeper,
// discard menu, calendar, field items, cards, fishing, poop, city, medals), and the adapters by type
// for the commands of the game and its menus. The session owns the adapters once given them: the
// typed pointers are valid while it lives.
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
  // The item keeper the story opens (CALL_ROUTINE 11).
  ItemKeeper* itemKeeper = nullptr;
  // The menu that picks an item from the bag (CALL_ROUTINE 0).
  DiscardMenu* discardMenu = nullptr;
  CalendarAdapter* calendar = nullptr;
  FieldItemsAdapter* fieldItems = nullptr;
  CardsAdapter* cards = nullptr;
  FishingAdapter* fishing = nullptr;
  // The partner's poop piles on the ground.
  PoopAdapter* poop = nullptr;
  // The city's prosperity and the medals the field gives.
  CityAdapter* city = nullptr;
  MedalsAdapter* medals = nullptr;
};

// The adapters of every game system on the packs, which must outlive them, wired to each other: the
// evolution reads the learned moves from the battle adapter.
GameSystems createGameSystems(const assets::VirtualFileSystem& files, SystemTables tables = {});

} // namespace dwe::session
