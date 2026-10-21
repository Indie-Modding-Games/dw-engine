#include <dwe/assets/file_system.hpp>
#include <dwe/core/json_schema.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/session/session_rules.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

#include "session_fixture.hpp"

using dwe::session::testing::errorOf;
using dwe::session::testing::newGame;
using dwe::session::testing::TestPacks;

namespace {

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

} // namespace

TEST_CASE("the session's sections follow their schemas") {
    const dwe::assets::DiskFileSystem disk;
    constexpr std::array<std::string_view, 4> Sections{"new_game", "tamer", "item_use", "partner_moves"};
    for (const std::string_view section : Sections) {
        CAPTURE(section);
        const auto schemaBytes =
            disk.readFile(std::filesystem::path{DWE_SCHEMA_DIR} / std::format("ruleset_{}.schema.json", section));
        REQUIRE_MESSAGE(schemaBytes.has_value(), errorOf(schemaBytes));
        const auto schemaText = dwe::core::parseYaml(textOf(*schemaBytes));
        REQUIRE_MESSAGE(schemaText.has_value(), errorOf(schemaText));
        const auto schema = dwe::core::JsonSchema::compile(*schemaText, {});
        REQUIRE_MESSAGE(schema.has_value(), errorOf(schema));
        const auto bytes =
            disk.readFile(std::filesystem::path{DWE_GAME_PACK} / std::format("rules/original/{}.yaml", section));
        REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
        const auto document = dwe::core::parseYaml(textOf(*bytes));
        REQUIRE_MESSAGE(document.has_value(), errorOf(document));
        for (const dwe::core::SchemaProblem& problem : schema->check(*document).problems) {
            FAIL_CHECK(std::format("{}:{}: {}: {}", problem.line, problem.column, problem.path, problem.message));
        }
    }
}

TEST_CASE("the original's tamer level runs from 0 to 10 and a new game starts at 0") {
    const TestPacks packs;
    const auto session = newGame(packs, nullptr);
    const dwe::session::SessionRules& rules = session->sessionRules();
    // tamer.c: addTamerLevel.
    CHECK(rules.tamer.levelMin == 0);
    CHECK(rules.tamer.levelMax == 10);
    // main_menu.c: initializeDefaultSavegame.
    CHECK(rules.newGame.tamerLevel == 0);
}

TEST_CASE("the session's checks refuse values it cannot use") {
    const TestPacks packs;
    const auto session = newGame(packs, nullptr);
    auto& store = session->rules();
    CHECK_FALSE(store.setValue("tamer", "level_min", "11").has_value());
    CHECK_FALSE(store.setValue("new_game", "tamer_level", "-1").has_value());
    CHECK_FALSE(store.setValue("new_game", "stats.hp", "-5").has_value());
    CHECK(store.setValue("new_game", "stats.hp", "600").has_value());
    session->tick(dwe::session::testing::Tick);
    CHECK(session->sessionRules().newGame.stats[dwe::game::partner::Stat::Hp] == 600);
}
