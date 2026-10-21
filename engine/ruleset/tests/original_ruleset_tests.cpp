#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/json_schema.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ruleset_fixture.hpp"

namespace dwe::ruleset {

namespace {

using testing::errorOf;

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// The tables the original ruleset names, by role (rules/original/ruleset.yaml).
constexpr std::array<std::string_view, 8> OriginalTables{"digimon", "evolution", "raise",        "training",
                                                         "moves",   "items",     "item_effects", "shops"};

} // namespace

TEST_CASE("the game pack's original ruleset loads, with the clock of the original") {
    const assets::DiskFileSystem disk;
    // The game pack depends on base; a stand-in base pack with empty tables lets it mount without the disc.
    assets::MemoryFileSystem memory;
    memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
    for (const std::string_view role : OriginalTables) {
        memory.addFile(std::format("base/data/{}.yaml", role), std::format("{}: []\n", role));
    }
    const std::array<assets::PackLocation, 2> packs{
        assets::PackLocation{
            .fileSystem = &memory, .root = "base", .fillsGaps = false, .origin = assets::PackOrigin::Engine},
        assets::PackLocation{
            .fileSystem = &disk, .root = DWE_GAME_PACK, .fillsGaps = false, .origin = assets::PackOrigin::Engine}};
    const auto vfs = assets::VirtualFileSystem::mount(packs);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    const auto ruleset = loadRuleset(*vfs, OriginalRuleset);
    REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
    CHECK(ruleset->info().chain == std::vector<std::string>{"original"});
    for (const std::string_view role : OriginalTables) {
        REQUIRE_MESSAGE(ruleset->table(role) != nullptr, role);
        CHECK(ruleset->table(role)->str() == std::format("data/{}", role));
    }
    CHECK(ruleset->section("care") != nullptr);
    const auto clock = readClockRules(*ruleset);
    REQUIRE_MESSAGE(clock.has_value(), errorOf(clock));
    // clock.c: tickGameClock and initializeClockData.
    CHECK(clock->framesPerMinute == 20);
    CHECK(clock->daysPerYear == 30);
    CHECK(clock->yearLimit == 100);
    CHECK(clock->start == GameTime{.year = 0, .day = 0, .hour = 8, .minute = 0});
}

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it). R3.2: the tables of the original
// ruleset are the ones dwx extracts from the disc: each is in the base pack and follows the schema the base pack gives
// it.
TEST_CASE(
    "the user's disc, extracted: every table of the original ruleset is in the base pack and follows its schema") {
    const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
    if (!mountedContent) {
        MESSAGE("DWE_CONTENT is not set: skipping the test on the extracted content");
        return;
    }
    const std::filesystem::path& content = *mountedContent;
    const assets::DiskFileSystem disk;
    auto found = assets::findPacks(disk, content);
    REQUIRE_MESSAGE(found.has_value(), errorOf(found));
    found->packs.push_back({.path = DWE_GAME_PACK, .kind = assets::FoundPack::Kind::Folder});
    const auto opened = assets::openPacks(disk, found->packs, dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(opened.has_value(), errorOf(opened));
    const auto vfs = assets::VirtualFileSystem::mount(opened->locations);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    const auto ruleset = loadRuleset(*vfs, OriginalRuleset);
    REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
    CHECK(ruleset->tables().size() == OriginalTables.size());

    constexpr std::array<std::string_view, 1> Annotations{"assetReference"};
    for (const auto& [role, id] : ruleset->tables()) {
        CAPTURE(role);
        std::string schemaName;
        for (const assets::MountedPack& pack : vfs->packs()) {
            for (const assets::SchemaRule& rule : pack.manifest.schemas) {
                for (const std::string& pattern : rule.ids) {
                    if (assets::matchesIdPattern(pattern, id)) {
                        schemaName = rule.schema;
                    }
                }
            }
        }
        REQUIRE_MESSAGE(!schemaName.empty(), std::format("no schema rule of the packs matches {}", id.str()));
        const auto schemaBytes =
            disk.readFile(std::filesystem::path{DWE_SCHEMA_DIR} / std::format("{}.schema.json", schemaName));
        REQUIRE_MESSAGE(schemaBytes.has_value(), errorOf(schemaBytes));
        const auto schemaText = core::parseYaml(textOf(*schemaBytes));
        REQUIRE_MESSAGE(schemaText.has_value(), errorOf(schemaText));
        const auto schema = core::JsonSchema::compile(*schemaText, Annotations);
        REQUIRE_MESSAGE(schema.has_value(), errorOf(schema));

        const auto bytes = vfs->read(id);
        REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
        const auto table = core::parseYaml(textOf(*bytes));
        REQUIRE_MESSAGE(table.has_value(), errorOf(table));
        const core::SchemaReport report = schema->check(*table);
        for (const core::SchemaProblem& problem : report.problems) {
            FAIL_CHECK(
                std::format("{} {}:{}: {}: {}", id.str(), problem.line, problem.column, problem.path, problem.message));
        }
        // Each table has lists of rows, and none of the original's is empty.
        REQUIRE(table->isMapping());
        CHECK_FALSE(table->entries().empty());
        for (const core::YamlEntry& entry : table->entries()) {
            CHECK_MESSAGE((!entry.value.isSequence() || !entry.value.items().empty()), entry.key);
        }
    }
}

} // namespace dwe::ruleset
