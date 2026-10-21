#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/game/partner/rules.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "partner_fixture.hpp"

namespace dwe::game::partner {

namespace {

// Made-up rows in the shape of the extracted tables.
std::string digimonRow(std::string_view id, std::int32_t level) {
    std::string moves;
    for (int move = 0; move < 16; ++move) {
        moves += "  - null\n";
    }
    return std::format("- id: \"{}\"\n  index: 1\n  name: \"{}\"\n  file_name: \"X\"\n  bone_count: 1\n  radius: 1\n"
                       "  height: 1\n  type: 0\n  level: {}\n  special:\n  - 0\n  - null\n  - null\n"
                       "  drop_item: null\n  drop_chance: 0\n  moves:\n{}",
                       id, id, level, moves);
}

std::string raiseRow(std::string_view digimon, std::int32_t sleepCycle) {
    return std::format("- digimon: {}\n  index: 1\n  hunger_times: [7, 12, 18, -1, -1, -1, -1, -1]\n"
                       "  energy_cap: 40\n  energy_threshold: 10\n  energy_usage: 2\n  poop_timer: 12\n"
                       "  unknown_0e: 0\n  poop_size: 6\n  favorite_food: \"fish\"\n  sleep_cycle: {}\n"
                       "  favored_region: 1\n  training_type: 2\n  default_weight: 15\n"
                       "  view: {{ x: 0, y: 0, z: 0 }}\n",
                       digimon, sleepCycle);
}

} // namespace

TEST_CASE("the species table joins the raise rows with the level of their digimon") {
    const auto digimon = assets::parseDigimonTable("digimon:\n" + digimonRow("hero", 0) + digimonRow("tama", 3));
    REQUIRE_MESSAGE(digimon.has_value(), testing::errorOf(digimon));
    const auto raise = assets::parseRaiseTable("raise:\n" + raiseRow("null", 0) + raiseRow("\"tama\"", 2));
    REQUIRE_MESSAGE(raise.has_value(), testing::errorOf(raise));
    const auto table = buildSpeciesTable(*digimon, *raise);
    REQUIRE_MESSAGE(table.has_value(), testing::errorOf(table));
    // A row with no digimon is skipped.
    CHECK(table->species().size() == 1);
    const SpeciesCare* tama = table->find("tama");
    REQUIRE(tama != nullptr);
    CHECK(tama->level == Level::Rookie);
    CHECK(tama->hungerHours[1] == 12);
    CHECK(tama->energyCap == 40);
    CHECK(tama->poopInterval == 12);
    CHECK(tama->favoriteFood == "fish");
    CHECK(tama->sleepPattern == 2);
    CHECK(tama->trainingAptitude == 2);
    CHECK(tama->defaultWeight == 15);
}

TEST_CASE("a raise row for a digimon that is not in the table is an error") {
    const auto digimon = assets::parseDigimonTable("digimon:\n" + digimonRow("tama", 3));
    REQUIRE(digimon.has_value());
    const auto raise = assets::parseRaiseTable("raise:\n" + raiseRow("\"ghost\"", 0));
    REQUIRE(raise.has_value());
    CHECK_FALSE(buildSpeciesTable(*digimon, *raise).has_value());
}

TEST_CASE("a raise row with a missing or unknown key is an error") {
    CHECK_FALSE(assets::parseRaiseTable("raise:\n- digimon: null\n").has_value());
    CHECK_FALSE(assets::parseRaiseTable("raise:\n" + raiseRow("null", 0) + "  extra: 1\n").has_value());
    CHECK_FALSE(assets::parseDigimonTable("digimon:\n- id: \"tama\"\n").has_value());
}

TEST_CASE("a species whose sleep pattern is not in the rules is refused") {
    testing::OriginalRuleset original;
    const PartnerRules rules = original.rules();
    SpeciesTable table;
    SpeciesCare broken = testing::rookieSpecies("broken");
    broken.sleepPattern = 99;
    table.add(broken);
    Partner partner(rules, table, 1);
    const ruleset::GameClock clock(ruleset::ClockRules{});
    CHECK_FALSE(partner.start("broken", testing::testStats(), clock).has_value());
    CHECK_FALSE(partner.start("nobody", testing::testStats(), clock).has_value());
}

TEST_CASE("the user's disc, extracted: every partner species loads and each rookie can be raised for a day") {
    const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
    if (!mountedContent) {
        MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
        return;
    }
    const std::filesystem::path& content = *mountedContent;
    const assets::DiskFileSystem disk;
    const auto packs = assets::findPacks(disk, content);
    REQUIRE_MESSAGE(packs.has_value(), testing::errorOf(packs));
    const auto opened = assets::openPacks(disk, packs->packs);
    REQUIRE_MESSAGE(opened.has_value(), testing::errorOf(opened));
    std::vector<assets::PackLocation> locations = opened->locations;
    locations.push_back({.fileSystem = &disk, .root = DWE_GAME_PACK});
    const auto vfs = assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(vfs.has_value(), testing::errorOf(vfs));
    const auto ruleset = ruleset::loadRuleset(*vfs, ruleset::OriginalRuleset, partnerSectionChecks());
    REQUIRE_MESSAGE(ruleset.has_value(), testing::errorOf(ruleset));
    const auto rules = loadPartnerRules(*ruleset);
    REQUIRE_MESSAGE(rules.has_value(), testing::errorOf(rules));
    const auto table = loadSpeciesTable(*vfs, *ruleset);
    REQUIRE_MESSAGE(table.has_value(), testing::errorOf(table));

    std::size_t rookies = 0;
    for (const auto& [id, species] : table->species()) {
        CAPTURE(id);
        if (species.level == Level::None) {
            continue;
        }
        CHECK(species.sleepPattern >= 0);
        CHECK(static_cast<std::size_t>(species.sleepPattern) < rules->care.sleep.patterns.size());
        if (species.level != Level::Rookie) {
            continue;
        }
        ++rookies;
        Partner partner(*rules, *table, 1);
        ruleset::GameClock clock(ruleset::ClockRules{});
        REQUIRE(partner.start(id, testing::testStats(), clock).has_value());
        const ruleset::ClockAdvance day = clock.advanceFrames(24LL * 60 * 20);
        partner.tick(clock, day, {});
        CHECK(partner.alive());
        CHECK(partner.state().life.age == 1);
    }
    CHECK(rookies > 0);
    // The rebirth babies are partner species.
    for (const RebirthBaby& baby : rules->life.rebirth.babies) {
        CAPTURE(baby.digimon);
        const SpeciesCare* found = table->find(baby.digimon);
        REQUIRE(found != nullptr);
        CHECK(found->level == Level::Baby);
    }
}

} // namespace dwe::game::partner
