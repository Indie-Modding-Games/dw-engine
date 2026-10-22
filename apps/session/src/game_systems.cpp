#include <dwe/assets/asset_id.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_systems.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_adapter.hpp>

#include <string_view>
#include <utility>

namespace dwe::session {
namespace {

// A table of the ruleset's manifest, by its role.
template <typename Table>
core::Result<Table>
loadRoleTable(const assets::VirtualFileSystem& files, const ruleset::Ruleset& rules, std::string_view role,
              core::Result<Table> (*load)(const assets::VirtualFileSystem&, const assets::AssetId&)) {
    const assets::AssetId* id = rules.table(role);
    if (id == nullptr) {
        return core::fail(messages::formatMessage(messages::NoMoveTable, role));
    }
    return load(files, *id);
}

} // namespace

core::Result<SystemTables> loadSystemTables(const assets::VirtualFileSystem& files, const ruleset::Ruleset& rules) {
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
    auto digimon =
        loadRoleTable<assets::DigimonTable>(files, rules, game::evolution::DigimonTableRole, assets::loadDigimonTable);
    if (!digimon) {
        return std::unexpected{digimon.error()};
    }
    auto moves = loadRoleTable<assets::MoveTable>(files, rules, game::evolution::MoveTableRole, assets::loadMoveTable);
    if (!moves) {
        return std::unexpected{moves.error()};
    }
    return SystemTables{.evolution = std::move(*evolution),
                        .items = std::move(*items),
                        .itemEffects = std::move(*effects),
                        .digimon = std::move(*digimon),
                        .moves = std::move(*moves)};
}

GameSystems createGameSystems(const assets::VirtualFileSystem& files, SystemTables tables) {
    // The story owns the raised forms the evolution rules read; a save from before the story section takes them from
    // the evolution section, which is read first.
    auto story = std::make_unique<StoryAdapter>(StoryAdapterDesc{.files = &files});
    auto evolution = std::make_unique<EvolutionAdapter>(
        EvolutionAdapterDesc{.files = &files, .tables = std::move(tables.evolution), .story = story.get()});
    story->setSavedRaised([evolution = evolution.get()] { return evolution->state().raised; });
    auto inventory = std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &files,
                                                                             .items = std::move(tables.items),
                                                                             .effects = std::move(tables.itemEffects),
                                                                             .evolution = evolution.get()});
    auto battle = std::make_unique<BattleAdapter>(BattleAdapterDesc{.files = &files,
                                                                    .digimon = std::move(tables.digimon),
                                                                    .moves = std::move(tables.moves),
                                                                    .inventory = inventory.get()});
    // The evolution requirements read the partner's learned moves from the battle adapter.
    evolution->setMoveSource(*battle);
    GameSystems game{.systems = {},
                     .evolution = evolution.get(),
                     .inventory = inventory.get(),
                     .battle = battle.get(),
                     .story = story.get()};
    game.systems.push_back(std::move(evolution));
    game.systems.push_back(std::move(inventory));
    game.systems.push_back(std::move(battle));
    game.systems.push_back(std::move(story));
    return game;
}

} // namespace dwe::session
