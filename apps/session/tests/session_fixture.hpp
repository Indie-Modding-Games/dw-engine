#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/session/game_session.hpp>

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace dwe::session::testing {

template <typename T>
std::string errorOf(const core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// The tables the original ruleset names, by role (rules/original/ruleset.yaml).
inline constexpr std::array<std::string_view, 8> OriginalTables{"digimon", "evolution", "raise",        "training",
                                                                "moves",   "items",     "item_effects", "shops"};

// A game loop's tick.
inline constexpr double Tick = 1.0 / 60.0;
// The tick of the tamer of the long raises: six game hours at sixty game hours per real second, as a fast-forward
// that skips frames would play.
inline constexpr double CareTick = 0.1;
// GameClock speeds: 1 is a game minute per real second.
inline constexpr double GameHourPerSecond = 60.0;
inline constexpr double SixtyGameHoursPerSecond = 3600.0;
// The date the saves of the tests are written on.
inline constexpr std::chrono::sys_seconds SaveDate{std::chrono::seconds{1'790'000'000}};

// The game pack, mounted over a stand-in base pack whose tables are empty, so the tests need no disc; `baseFiles`
// adds documents to the base pack, by path inside it. Not movable: the file systems must stay where the mounted packs
// point.
class TestPacks {
public:
    explicit TestPacks(std::initializer_list<std::pair<std::string_view, std::string_view>> baseFiles = {}) {
        memory_.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
        for (const std::string_view role : OriginalTables) {
            memory_.addFile(std::format("base/data/{}.yaml", role), std::format("{}: []\n", role));
        }
        for (const auto& [path, text] : baseFiles) {
            memory_.addFile(std::format("base/{}", path), text);
        }
        const std::array<assets::PackLocation, 2> packs{
            assets::PackLocation{
                .fileSystem = &memory_, .root = "base", .fillsGaps = false, .origin = assets::PackOrigin::Engine},
            assets::PackLocation{
                .fileSystem = &disk_, .root = DWE_GAME_PACK, .fillsGaps = false, .origin = assets::PackOrigin::Engine}};
        auto vfs = assets::VirtualFileSystem::mount(packs);
        REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
        files_ = std::make_unique<assets::VirtualFileSystem>(std::move(*vfs));
    }
    TestPacks(const TestPacks&) = delete;
    TestPacks& operator=(const TestPacks&) = delete;
    TestPacks(TestPacks&&) = delete;
    TestPacks& operator=(TestPacks&&) = delete;
    ~TestPacks() = default;

    [[nodiscard]] const assets::VirtualFileSystem& files() const { return *files_; }

private:
    assets::MemoryFileSystem memory_;
    assets::DiskFileSystem disk_;
    std::unique_ptr<assets::VirtualFileSystem> files_;
};

// Made-up species in the shape of the raise table, not the original's data: the new game's partner and the babies
// it is reborn as.
inline game::partner::SpeciesCare madeUpSpecies(std::string id, game::partner::Level level) {
    return {.id = std::move(id),
            .level = level,
            .hungerHours = {8, 13, 19, -1, -1, -1, -1, -1},
            .energyCap = 60,
            .energyThreshold = 15,
            .energyUsage = 3,
            .poopInterval = 18,
            .poopSize = 8,
            .favoriteFood = "meat",
            .sleepPattern = 2,
            .favoredRegion = 2,
            .trainingAptitude = 1,
            .defaultWeight = 20};
}

inline std::shared_ptr<const game::partner::SpeciesTable> madeUpTable() {
    game::partner::SpeciesTable table;
    table.add(madeUpSpecies("agum", game::partner::Level::Rookie));
    for (const std::string_view baby : {"bota", "puni", "poyo", "yura"}) {
        game::partner::SpeciesCare care = madeUpSpecies(std::string{baby}, game::partner::Level::Baby);
        care.hungerHours = {-1, -1, -1, -1, -1, -1, -1, -1};
        care.sleepPattern = 6;
        table.add(std::move(care));
    }
    return std::make_shared<const game::partner::SpeciesTable>(std::move(table));
}

// A session on the test packs with the made-up species, saving to `storage`, its game started.
inline std::unique_ptr<GameSession> newGame(const TestPacks& packs, game::save::SaveStorage* storage,
                                            std::uint64_t seed = 7) {
    auto session = GameSession::create({.files = &packs.files(),
                                        .rulesetId = "original",
                                        .rules = std::nullopt,
                                        .species = madeUpTable(),
                                        .seed = seed,
                                        .storage = storage,
                                        .gameVersion = "test",
                                        .wallClock = [] { return SaveDate; },
                                        .systems = {}});
    REQUIRE_MESSAGE(session.has_value(), errorOf(session));
    auto started = (*session)->startNewGame();
    REQUIRE_MESSAGE(started.has_value(), errorOf(started));
    return std::move(*session);
}

// A tamer that answers every need as soon as it shows, at the end of each tick: feeds, takes it to the toilet, cures
// it and puts it to bed.
inline void careFor(GameSession& session, double realSeconds, double tick = CareTick) {
    game::partner::FoodEffect meal;
    meal.item = "meat";
    meal.energy = 20;
    const auto ticks = static_cast<std::int64_t>(realSeconds / tick);
    for (std::int64_t step = 0; step < ticks; ++step) {
        session.tick(tick);
        const game::partner::Conditions& conditions = session.partner().state().conditions;
        if (conditions.has(game::partner::Condition::Hungry)) {
            session.feed(meal);
        }
        if (conditions.has(game::partner::Condition::NeedsToilet)) {
            session.useToilet();
        }
        if (conditions.has(game::partner::Condition::Sick) || conditions.has(game::partner::Condition::Injured)) {
            session.cure({.injuryPercent = 100, .sicknessPercent = 100});
        }
        if (session.partner().canSleep()) {
            CHECK(session.sleep({}).has_value());
        }
    }
}

} // namespace dwe::session::testing
