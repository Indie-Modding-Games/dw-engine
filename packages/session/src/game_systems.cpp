#include <dwe/assets/asset_id.hpp>
#include <dwe/core/log.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/species_extra.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/battle_registry.hpp>
#include <dwe/session/bird_transport.hpp>
#include <dwe/session/calendar_adapter.hpp>
#include <dwe/session/card_shop.hpp>
#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/city_adapter.hpp>
#include <dwe/session/curling_adapter.hpp>
#include <dwe/session/discard_menu.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/fishing_adapter.hpp>
#include <dwe/session/game_systems.hpp>
#include <dwe/session/gym_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/item_keeper.hpp>
#include <dwe/session/item_trader.hpp>
#include <dwe/session/jukebox.hpp>
#include <dwe/session/map_visits.hpp>
#include <dwe/session/medals_adapter.hpp>
#include <dwe/session/merit_shop.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/poop_adapter.hpp>
#include <dwe/session/shop_counter.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_game_orders.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/session/tournament_adapter.hpp>
#include <dwe/session/weather_seed.hpp>

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
  if (rules.table(game::evolution::DigimonTableRole) == nullptr) {
    return core::fail(
        messages::formatMessage(messages::NoMoveTable, game::evolution::DigimonTableRole));
  }
  auto digimon = ruleset::loadPatchedDigimonTable(files, rules);
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
  // The game passes no tables (session_start.cpp): the cards then read theirs from the packs, as
  // the inventory does, or the player would never hold a card.
  if (!tables.cards) {
    if (auto cards = assets::loadCardTable(files); cards) {
      tables.cards = std::move(*cards);
    } else {
      core::logWarning("{}", cards.error().message);
    }
  }
  // The fishing's tables too, or no map would have water and the pause menu would never offer Fish.
  if (!tables.fishing) {
    if (auto fishing = assets::loadFishingTable(files); fishing) {
      tables.fishing = std::move(*fishing);
    } else {
      core::logWarning("{}", fishing.error().message);
    }
  }
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
  auto calendar = std::make_unique<CalendarAdapter>(*story);
  auto fieldItems = std::make_unique<FieldItemsAdapter>(*inventory, *story);
  auto storyOrders =
      std::make_unique<StoryGameOrders>(StoryGameOrdersDesc{.files = &files,
                                                            .story = story.get(),
                                                            .inventory = inventory.get(),
                                                            .battle = battle.get(),
                                                            .calendar = calendar.get(),
                                                            .fieldItems = fieldItems.get(),
                                                            .cards = cards.get()});
  auto shop = std::make_unique<ShopCounter>(
      ShopCounterDesc{.files = &files, .inventory = inventory.get(), .story = story.get()});
  // handleItemLoss: what a lost battle takes goes to the recycle shop.
  inventory->setLossListener([shop = shop.get()](const std::vector<game::inventory::Stack>& lost) {
    shop->recycleLost(lost);
  });
  auto itemKeeper =
      std::make_unique<ItemKeeper>(ItemKeeperDesc{.files = &files, .inventory = inventory.get()});
  // dailyPStatTrigger: each new day also rolls the card shop's stock; the shop trades after the
  // cards started.
  auto cardShop = std::make_unique<CardShop>(
      CardShopDesc{.files = &files, .cards = cards.get(), .inventory = inventory.get()});
  calendar->setNewDayListener(
      [shop = cardShop.get()](std::int32_t days) { shop->startNewDays(days); });
  auto meritShop = std::make_unique<MeritShop>(MeritShopDesc{
      .files = &files, .cards = cards.get(), .inventory = inventory.get(), .story = story.get()});
  auto discardMenu = std::make_unique<DiscardMenu>(
      DiscardMenuDesc{.files = &files, .inventory = inventory.get(), .story = story.get()});
  auto jukebox = std::make_unique<Jukebox>(JukeboxDesc{.files = &files, .story = story.get()});
  auto itemTrader = std::make_unique<ItemTrader>(
      ItemTraderDesc{.files = &files, .inventory = inventory.get(), .story = story.get()});
  auto birdTransport = std::make_unique<BirdTransport>(
      BirdTransportDesc{.files = &files, .inventory = inventory.get(), .story = story.get()});
  GameSystems game{.systems = {},
                   .evolution = evolution.get(),
                   .inventory = inventory.get(),
                   .battle = battle.get(),
                   .story = story.get(),
                   .storyOrders = storyOrders.get(),
                   .shop = shop.get(),
                   .itemKeeper = itemKeeper.get(),
                   .discardMenu = discardMenu.get(),
                   .jukebox = jukebox.get(),
                   .itemTrader = itemTrader.get(),
                   .birdTransport = birdTransport.get()};
  // The story's scenes tick before the story, so what the app reported is seen the same tick.
  auto scenes = std::make_unique<StoryScenes>(StoryScenesDesc{.story = story.get(),
                                                              .evolution = evolution.get(),
                                                              .inventory = inventory.get(),
                                                              .battle = battle.get(),
                                                              .cardShop = cardShop.get()});
  game.scenes = scenes.get();
  game.systems.push_back(std::move(evolution));
  game.systems.push_back(std::move(inventory));
  game.systems.push_back(std::move(battle));
  game.systems.push_back(std::move(storyOrders));
  game.systems.push_back(std::move(scenes));
  game.systems.push_back(std::move(story));
  game.systems.push_back(std::make_unique<GymAdapter>());
  game.systems.push_back(std::move(shop));
  game.systems.push_back(std::move(itemKeeper));
  game.systems.push_back(std::move(discardMenu));
  game.systems.push_back(std::move(jukebox));
  game.systems.push_back(std::move(itemTrader));
  game.systems.push_back(std::move(birdTransport));
  // The calendar runs a new game's day on the story after the story started it.
  game.calendar = calendar.get();
  game.systems.push_back(std::move(calendar));
  game.fieldItems = fieldItems.get();
  game.systems.push_back(std::move(fieldItems));
  game.cards = cards.get();
  game.systems.push_back(std::move(cards));
  game.cardShop = cardShop.get();
  game.systems.push_back(std::move(cardShop));
  game.meritShop = meritShop.get();
  game.systems.push_back(std::move(meritShop));
  auto fishing =
      std::make_unique<FishingAdapter>(*game.inventory, *game.story, std::move(tables.fishing));
  game.fishing = fishing.get();
  game.systems.push_back(std::move(fishing));
  auto tournament =
      std::make_unique<TournamentAdapter>(TournamentAdapterDesc{.files = &files,
                                                                .story = game.story,
                                                                .battle = game.battle,
                                                                .inventory = game.inventory,
                                                                .cards = game.cards});
  game.tournament = tournament.get();
  game.scenes->attachTournament(game.tournament);
  // A species' cup gate reads the cups won.
  game.evolution->setCupSource(*game.tournament);
  game.systems.push_back(std::move(tournament));
  auto curling = std::make_unique<CurlingAdapter>(CurlingAdapterDesc{.story = game.story});
  game.curling = curling.get();
  game.systems.push_back(std::move(curling));
  auto poop = std::make_unique<PoopAdapter>();
  game.poop = poop.get();
  game.systems.push_back(std::move(poop));
  // After the story, whose digimon table and new game they read.
  auto city = std::make_unique<CityAdapter>(*game.story);
  game.city = city.get();
  game.systems.push_back(std::move(city));
  auto medals = std::make_unique<MedalsAdapter>(MedalsAdapterDesc{.story = game.story,
                                                                  .battle = game.battle,
                                                                  .inventory = game.inventory,
                                                                  .cards = game.cards});
  game.medals = medals.get();
  game.tournament->attachMedals(game.medals);
  game.systems.push_back(std::move(medals));
  auto registry = std::make_unique<BattleRegistry>();
  game.registry = registry.get();
  game.systems.push_back(std::move(registry));
  auto mapVisits = std::make_unique<MapVisits>();
  game.mapVisits = mapVisits.get();
  game.systems.push_back(std::move(mapVisits));
  auto weather = std::make_unique<WeatherSeed>();
  game.weather = weather.get();
  game.systems.push_back(std::move(weather));
  return game;
}

} // namespace dwe::session
