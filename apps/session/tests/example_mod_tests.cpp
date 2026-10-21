// The synthetic example mod of tests/mods/example_mod (tests/mods/README.md) over a stand-in base pack: it adds a
// Digimon and a map by id, and the engine uses them with no code of their own.
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/text_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/scene/world.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/game_systems.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/assets/tests/map_documents.hpp"
#include "fixture.hpp"
#include "session_fixture.hpp"

namespace {

using dwe::assets::MapDocument;
using dwe::assets::VirtualFileSystem;
using dwe::game::field::Field;
using dwe::game::field::MapChange;
using dwe::session::GameSession;
using dwe::session::testing::errorOf;

constexpr std::string_view NewSpecies = "example_mod:pebblemon";
constexpr std::string_view NewMap = "example_mod:meadow";
constexpr std::string_view OriginalMap = "twna01";
constexpr float Tick = 1.0F / 60.0F;
constexpr int MaxWalkTicks = 1200;

// A made-up row of data/digimon, in the shape the base pack has.
std::string madeUpDigimon(std::string_view id, int index, int level) {
    return std::format(R"(- id: "{}"
  index: {}
  name: "{}"
  file_name: null
  bone_count: 1
  radius: 100
  height: 200
  type: 1
  level: {}
  special: [0, null, null]
  drop_item: null
  drop_chance: 0
  moves: [null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null]
)",
                       id, index, id, level);
}

// A made-up row of data/raise.
std::string madeUpRaise(std::string_view id, int index) {
    return std::format(R"(- digimon: "{}"
  index: {}
  hunger_times: [8, 13, 19, -1, -1, -1, -1, -1]
  energy_cap: 60
  energy_threshold: 15
  energy_usage: 3
  poop_timer: 18
  unknown_0e: 0
  poop_size: 8
  favorite_food: "meat"
  sleep_cycle: 2
  favored_region: 2
  training_type: 1
  default_weight: 20
  view: {{x: 0, y: 0, z: 0}}
)",
                       id, index);
}

// The original map with its exit 0 (trigger 110 at column 10 of row 20) leading to the mod's map: what a pack that
// replaces a map's data whole, as the map editor writes it, leaves.
std::string originalMapToNewMap() {
    std::string map = dwe::assets::testing::fixtureMap();
    const std::string exit = "target_map: 218\n  target_map_id: \"room08\"\n  target_exit: 2";
    map.replace(map.find(exit), exit.size(),
                std::format("target_map: 0\n  target_map_id: \"{}\"\n  target_exit: 0", NewMap));
    return map;
}

// The game pack and the example mod over a stand-in base pack with an original Digimon, its babies and an original
// map, and a second mod that turns the original map's exit 0 toward the mod's map.
class ExampleModPacks {
public:
    ExampleModPacks() {
        memory_.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
        for (const std::string_view role : dwe::session::testing::OriginalTables) {
            memory_.addFile(std::format("base/data/{}.yaml", role), std::format("{}: []\n", role));
        }
        std::string digimon = "digimon:\n";
        std::string raise = "raise:\n";
        int index = 0;
        for (const std::string_view id : {"bota", "puni", "poyo", "yura", "agum"}) {
            digimon += madeUpDigimon(id, index, id == "agum" ? 3 : 1);
            raise += madeUpRaise(id, index);
            ++index;
        }
        memory_.addFile("base/data/digimon.yaml", digimon);
        memory_.addFile("base/data/raise.yaml", raise);
        memory_.addFile("base/data/maps.yaml", dwe::assets::testing::FixtureTable);
        memory_.addFile(std::format("base/map/{}/data.yaml", OriginalMap), dwe::assets::testing::fixtureMap());
        memory_.addFile("link/pack.yaml", "id: link\nversion: 1.0.0\nlayer: mods\npriority: 1\n");
        memory_.addFile(std::format("link/map/{}/data.yaml", OriginalMap), originalMapToNewMap());
        const std::array<dwe::assets::PackLocation, 4> packs{
            dwe::assets::PackLocation{.fileSystem = &memory_, .root = "base"},
            dwe::assets::PackLocation{.fileSystem = &disk_, .root = DWE_GAME_PACK},
            dwe::assets::PackLocation{.fileSystem = &disk_, .root = DWE_EXAMPLE_MOD},
            dwe::assets::PackLocation{.fileSystem = &memory_, .root = "link"}};
        auto vfs = VirtualFileSystem::mount(packs);
        REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
        files_ = std::make_unique<VirtualFileSystem>(std::move(*vfs));
    }

    [[nodiscard]] const VirtualFileSystem& files() const { return *files_; }

private:
    dwe::assets::MemoryFileSystem memory_;
    dwe::assets::DiskFileSystem disk_;
    std::unique_ptr<VirtualFileSystem> files_;
};

MapDocument loadedMap(const VirtualFileSystem& files, std::string_view id) {
    auto map = dwe::assets::loadMapDocument(files, id);
    REQUIRE_MESSAGE(map.has_value(), errorOf(map));
    return std::move(*map);
}

// The original ruleset with the mod's Digimon as the new game's partner.
dwe::ruleset::Ruleset rulesWithNewPartner(const VirtualFileSystem& files) {
    auto rules = dwe::ruleset::loadRuleset(files, dwe::ruleset::OriginalRuleset);
    REQUIRE_MESSAGE(rules.has_value(), errorOf(rules));
    REQUIRE(rules->setValue("new_game", "partner", std::format("\"{}\"", NewSpecies)).has_value());
    return std::move(*rules);
}

// A session with the mod's Digimon as the new game's partner, its game started.
std::unique_ptr<GameSession> newGameWithNewPartner(const VirtualFileSystem& files,
                                                   std::vector<std::unique_ptr<dwe::session::SessionSystem>> systems) {
    auto session = GameSession::create({.files = &files,
                                        .rulesetId = std::string{dwe::ruleset::OriginalRuleset},
                                        .rules = rulesWithNewPartner(files),
                                        .species = nullptr,
                                        .seed = 7,
                                        .storage = nullptr,
                                        .gameVersion = "test",
                                        .wallClock = [] { return dwe::session::testing::SaveDate; },
                                        .systems = std::move(systems)});
    REQUIRE_MESSAGE(session.has_value(), errorOf(session));
    auto started = (*session)->startNewGame();
    REQUIRE_MESSAGE(started.has_value(), errorOf(started));
    return std::move(*session);
}

// Pushes the stick east until the field leaves the map.
std::optional<MapChange> walkEastToWarp(Field& field) {
    field.setInput({.stick = {1.0F, 0.0F}, .run = true});
    std::optional<MapChange> change;
    for (int tick = 0; tick < MaxWalkTicks && !change; ++tick) {
        field.tick(Tick);
        change = field.takeMapChange();
    }
    return change;
}

} // namespace

TEST_CASE("the example mod adds its Digimon and its map to the base pack's tables by id") {
    const ExampleModPacks packs;
    const auto id = dwe::assets::AssetId::parse("data/digimon");
    REQUIRE(id.has_value());
    const auto digimon = dwe::assets::loadDigimonTable(packs.files(), *id);
    REQUIRE_MESSAGE(digimon.has_value(), errorOf(digimon));
    REQUIRE(digimon->digimon.size() == 6);
    CHECK(digimon->digimon.back().id == NewSpecies);
    CHECK(digimon->find(NewSpecies)->index == 5);

    const auto maps = dwe::assets::loadMapTable(packs.files());
    REQUIRE_MESSAGE(maps.has_value(), errorOf(maps));
    const dwe::assets::MapEntry* meadow = maps->find(NewMap);
    REQUIRE(meadow != nullptr);
    CHECK(meadow->index == 220); // after the fixture's last map, 219
    CHECK(maps->find(OriginalMap)->index == 204);
}

TEST_CASE("the field enters the example mod's map by a warp of an original map and leaves it by its own") {
    const ExampleModPacks packs;
    const MapDocument original = loadedMap(packs.files(), OriginalMap);
    dwe::scene::World world;
    Field field{world, dwe::game::field::testing::originalRules()};
    // West of the exit's trigger cell, column 10 of row 20.
    field.enterMapAt(original, {.position = {-4050.0F, 0.0F, -2950.0F}, .yaw = 0.0F});
    const std::optional<MapChange> toNewMap = walkEastToWarp(field);
    if (!toNewMap) {
        FAIL("no warp to the example mod's map");
        return;
    }
    CHECK(toNewMap->map == NewMap);

    const MapDocument meadow = loadedMap(packs.files(), toNewMap->map);
    CHECK(meadow.assets.backgrounds.size() == 1);
    REQUIRE(field.enterMap(meadow, toNewMap->exit).has_value());
    CHECK(field.mapId() == NewMap);

    const std::optional<MapChange> back = walkEastToWarp(field);
    if (!back) {
        FAIL("no warp out of the example mod's map");
        return;
    }
    CHECK(back->map == OriginalMap);
    CHECK(back->exit == 1);
}

TEST_CASE("a new game starts with the example mod's Digimon as the partner") {
    const ExampleModPacks packs;
    const std::unique_ptr<GameSession> session = newGameWithNewPartner(packs.files(), {});
    CHECK(session->partner().state().species == NewSpecies);
}

TEST_CASE("the example mod's texts name its Digimon and its map in every language it has") {
    const ExampleModPacks packs;
    for (const std::string_view language : {"en", "es"}) {
        const auto id = dwe::assets::AssetId::parse(std::format("text/{}/example_mod", language));
        REQUIRE(id.has_value());
        const auto texts = dwe::assets::readTextTable(
            packs.files(), *id,
            {.kind = dwe::assets::TextTableKind::Game, .language = std::string{language}, .name = "example_mod"});
        REQUIRE_MESSAGE(texts.has_value(), errorOf(texts));
        CHECK(texts->contains("example_mod.digimon_names.pebblemon"));
        CHECK(texts->contains("example_mod.map_names.meadow"));
    }
}

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it).
TEST_CASE("the user's disc, extracted: the example mod's Digimon is a partner with every system of the game") {
    const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
    if (!content) {
        MESSAGE("DWE_CONTENT is not set: skipping the example mod on extracted content");
        return;
    }
    const dwe::assets::DiskFileSystem disk;
    const std::array<dwe::assets::PackLocation, 3> locations{
        dwe::assets::PackLocation{.fileSystem = &disk, .root = *content / "base"},
        dwe::assets::PackLocation{.fileSystem = &disk, .root = DWE_GAME_PACK},
        dwe::assets::PackLocation{.fileSystem = &disk, .root = DWE_EXAMPLE_MOD}};
    auto files = VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(files.has_value(), errorOf(files));
    dwe::session::GameSystems systems = dwe::session::createGameSystems(*files);
    const std::unique_ptr<GameSession> session = newGameWithNewPartner(*files, std::move(systems.systems));
    CHECK(session->partner().state().species == NewSpecies);
    session->tick(1.0);
    CHECK(dwe::assets::loadMapDocument(*files, NewMap).has_value());
}
