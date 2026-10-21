#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/battle/simulation.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "fixture.hpp"

using namespace battle_test;

namespace {

struct ExtractedTables {
    dwe::assets::MoveTable moves;
    dwe::assets::DigimonTable digimon;
};

// The moves and Digimon tables extracted from the user's disc, or nothing when DWE_CONTENT is not set.
std::optional<ExtractedTables> extractedTables() {
    const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
    if (!mountedContent) {
        MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
        return std::nullopt;
    }
    const std::filesystem::path& content = *mountedContent;
    const dwe::assets::DiskFileSystem disk;
    const auto found = dwe::assets::findPacks(disk, content);
    REQUIRE_MESSAGE(found.has_value(), found.error().message);
    const auto opened = dwe::assets::openPacks(disk, found->packs, dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(opened.has_value(), opened.error().message);
    const auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
    REQUIRE_MESSAGE(vfs.has_value(), vfs.error().message);
    auto moves = dwe::assets::loadMoveTable(*vfs, *dwe::assets::AssetId::parse("data/moves"));
    REQUIRE_MESSAGE(moves.has_value(), moves.error().message);
    auto digimon = dwe::assets::loadDigimonTable(*vfs, *dwe::assets::AssetId::parse("data/digimon"));
    REQUIRE_MESSAGE(digimon.has_value(), digimon.error().message);
    return ExtractedTables{.moves = std::move(*moves), .digimon = std::move(*digimon)};
}

constexpr std::int32_t StatsPerLevel = 100;
constexpr std::int32_t OutOfReachMp = 9999;

// A fighter of a species with the first moves of its list and stats by its level (made up for the test).
Combatant speciesFighter(const dwe::assets::DigimonRow& species, const MoveCatalog& moves, Point at) {
    Combatant combatant = combatantOf(species);
    const std::int32_t value = StatsPerLevel * std::max(species.level, 1);
    combatant.stats = evenStats(value * 3, value);
    // Plenty of MP: in the original a fighter out of MP waits for good, so two of them never end the battle.
    combatant.stats.maxMp = OutOfReachMp;
    combatant.hp = combatant.stats.maxHp;
    combatant.mp = combatant.stats.maxMp;
    std::size_t slot = 0;
    for (const std::optional<std::string>& move : combatant.speciesMoves) {
        if (!move || slot == FinisherSlot) {
            continue;
        }
        const auto ref = moves.find(*move);
        if (ref && !moves.at(*ref).finisher && !moves.at(*ref).counter) {
            combatant.moves.at(slot++) = *move;
        }
    }
    combatant.movePriorities = {10, 10, 10, 0};
    combatant.position = at;
    return combatant;
}

} // namespace

TEST_CASE("the user's disc, extracted: every move builds into the battle catalog") {
    const auto tables = extractedTables();
    if (!tables) {
        return;
    }
    const auto catalog = MoveCatalog::build(tables->moves, originalRules());
    REQUIRE_MESSAGE(catalog, errorOf(catalog));
    CHECK(catalog->size() == tables->moves.moves.size());
    std::int32_t finishers = 0;
    for (MoveRef ref = 0; ref < catalog->size(); ++ref) {
        finishers += catalog->at(ref).finisher ? 1 : 0;
    }
    CHECK(finishers > 0);
}

TEST_CASE("the user's disc, extracted: species fight each other to an end, pair by pair") {
    const auto tables = extractedTables();
    if (!tables) {
        return;
    }
    const auto catalog = MoveCatalog::build(tables->moves, originalRules());
    REQUIRE_MESSAGE(catalog, errorOf(catalog));
    std::vector<const dwe::assets::DigimonRow*> fighters;
    for (const dwe::assets::DigimonRow& entry : tables->digimon.digimon) {
        if (entry.radius > 0 && entry.level > 0 && entry.moves.front()) {
            fighters.push_back(&entry);
        }
    }
    REQUIRE(fighters.size() > 10);
    constexpr std::size_t Stride = 7;
    std::int32_t battles = 0;
    for (std::size_t a = 0; a < fighters.size(); a += Stride) {
        for (std::size_t b = 0; b < fighters.size(); b += Stride) {
            BattleSetup setup;
            setup.partner = speciesFighter(*fighters[a], *catalog, {.x = 0, .z = 0});
            setup.enemies.push_back(speciesFighter(*fighters[b], *catalog, {.x = 0, .z = 900}));
            setup.condition.discipline = 50;
            const auto rates = runBattles(setup, originalRules(), *catalog, [] { return originalStrategies(); },
                                          {.count = 1, .firstSeed = (a * 1000) + b, .frameLimit = DefaultFrameLimit});
            REQUIRE_MESSAGE(rates, errorOf(rates));
            CHECK_MESSAGE(rates->unfinished == 0, fighters[a]->id, " against ", fighters[b]->id);
            ++battles;
        }
    }
    MESSAGE(battles, " battles");
}
