#include <dwe/assets/asset_id.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/calendar_adapter.hpp>
#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/fishing_adapter.hpp>
#include <dwe/session/game_systems.hpp>
#include <dwe/session/gym_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/shop_counter.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_game_orders.hpp>
#include <dwe/session/story_scenes.hpp>

#include <string_view>
#include <utility>
#include <vector>

namespace dwe::session {
namespace {

// A table of the ruleset's manifest, by its role.
template <typename Table>
core::Result<Table> loadRoleTable(const assets::VirtualFileSystem& files,
                                  const ruleset::Ruleset& rules, std::string_view role,
                                  core::Result<Table> (*load)(const assets::VirtualFileSystem&,
                                                              const assets::AssetId&)) {
  const assets::AssetId* id = rules.table(role);
  if (id == nullptr) {
    return core::fail(messages::formatMessage(messages::NoMoveTable, role));
  }
  return load(files, *id);
}

} // namespace

core::Result<SystemTables> loadSystemTables(const assets::VirtualFileSystem& files,
                                            const ruleset::Ruleset& rules) {
  auto evolution = game::evolution::EvolutionTables::load(files, rules);
  if (!evolution) {
    return std::unexpected{evolution.error()};
  }
  auto items = assets::loadItemTable(files);
  if (!items) {
    return std::unexpected{items.error()};
  }
  auto effects = assets::loadItemEffectTable(files);
  if (!effects) {
    return std::unexpected{effects.error()};
  }
  auto digimon = loadRoleTable<assets::DigimonTable>(
      files, rules, game::evolution::DigimonTableRole, assets::loadDigimonTable);
  if (!digimon) {
    return std::unexpected{digimon.error()};
  }
  auto moves = loadRoleTable<assets::MoveTable>(files, rules, game::evolution::MoveTableRole,
                                                assets::loadMoveTable);
  if (!moves) {
    return std::unexpected{moves.error()};
  }
  auto cards = assets::loadCardTable(files);
  if (!cards) {
    return std::unexpected{cards.error()};
  }
  auto fishing = assets::loadFishingTable(files);
  if (!fishing) {
    return std::unexpected{fishing.error()};
  }
  return SystemTables{.evolution = std::move(*evolution),
                      .items = std::move(*items),
                      .itemEffects = std::move(*effects),
                      .digimon = std::move(*digimon),
                      .moves = std::move(*moves),
                      .cards = std::move(*cards),
                      .fishing = std::move(*fishing)};
}

GameSystems createGameSystems(const assets::VirtualFileSystem& files, SystemTables tables) {
  // The story owns the raised forms the evolution rules read; a save from before the story section
  // takes them from the evolution section, which is read first.
  auto story = std::make_unique<StoryAdapter>(StoryAdapterDesc{.files = &files});
  auto evolution = std::make_unique<EvolutionAdapter>(EvolutionAdapterDesc{
      .files = &files, .tables = std::move(tables.evolution), .story = story.get()});
  story->setSavedRaised([evolution = evolution.get()] { return evolution->state().raised; });
  // The cards name their Digimon by number; the table goes to the battle next.
  auto cards = std::make_unique<CardsAdapter>(*story, std::move(tables.cards),
                                              tables.digimon ? &*tables.digimon : nullptr);
  auto inventory = std::make_unique<InventoryAdapter>(
      InventoryAdapterDesc{.files = &files,
                           .items = std::move(tables.items),
                           .effects = std::move(tables.itemEffects),
                           .evolution = evolution.get()});
  auto battle =
      std::make_unique<BattleAdapter>(BattleAdapterDesc{.files = &files,
                                                        .digimon = std::move(tables.digimon),
                                                        .moves = std::move(tables.moves),
                                                        .inventory = inventory.get()});
  // The evolution requirements read the partner's learned moves from the battle adapter.
  evolution->setMoveSource(*battle);
  auto storyOrders = std::make_unique<StoryGameOrders>(StoryGameOrdersDesc{
      .files = &files, .story = story.get(), .inventory = inventory.get(), .battle = battle.get()});
  auto shop = std::make_unique<ShopCounter>(
      ShopCounterDesc{.files = &files, .inventory = inventory.get(), .story = story.get()});
  // handleItemLoss: what a lost battle takes goes to the recycle shop.
  inventory->setLossListener([shop = shop.get()](const std::vector<game::inventory::Stack>& lost) {
    shop->recycleLost(lost);
  });
  GameSystems game{.systems = {},
                   .evolution = evolution.get(),
                   .inventory = inventory.get(),
                   .battle = battle.get(),
                   .story = story.get(),
                   .storyOrders = storyOrders.get(),
                   .shop = shop.get()};
  // The story's scenes tick before the story, so what the app reported is seen the same tick.
  auto scenes = std::make_unique<StoryScenes>(StoryScenesDesc{.story = story.get(),
                                                              .evolution = evolution.get(),
                                                              .inventory = inventory.get(),
                                                              .battle = battle.get()});
  game.scenes = scenes.get();
  game.systems.push_back(std::move(evolution));
  game.systems.push_back(std::move(inventory));
  game.systems.push_back(std::move(battle));
  game.systems.push_back(std::move(storyOrders));
  game.systems.push_back(std::move(scenes));
  game.systems.push_back(std::move(story));
  game.systems.push_back(std::make_unique<GymAdapter>());
  game.systems.push_back(std::move(shop));
  // The calendar runs a new game's day on the story after the story started it.
  auto calendar = std::make_unique<CalendarAdapter>(*game.story);
  game.calendar = calendar.get();
  game.systems.push_back(std::move(calendar));
  auto fieldItems = std::make_unique<FieldItemsAdapter>(*game.inventory, *game.story);
  game.fieldItems = fieldItems.get();
  game.systems.push_back(std::move(fieldItems));
  game.cards = cards.get();
  game.systems.push_back(std::move(cards));
  auto fishing =
      std::make_unique<FishingAdapter>(*game.inventory, *game.story, std::move(tables.fishing));
  game.fishing = fishing.get();
  game.systems.push_back(std::move(fishing));
  return game;
}

} // namespace dwe::session
