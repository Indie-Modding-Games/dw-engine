#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/rules.hpp>
#include <dwe/game/evolution/tables.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::game::evolution::testing {

// The message of a failed result. doctest builds a message even when the check passes, so it cannot call error().
template <typename T>
std::string errorOf(const core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

inline constexpr std::string_view SectionFile = DWE_GAME_PACK "/rules/original/evolution.yaml";

// The text of the game pack's evolution section.
inline std::string originalSectionText() {
    std::ifstream file{std::filesystem::path{SectionFile}};
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// A section parsed from a text, as the ruleset hands it out.
inline ruleset::Section sectionFrom(std::string_view text) {
    auto node = core::parseYaml(text);
    REQUIRE_MESSAGE(node.has_value(), errorOf(node));
    return {.name = std::string{EvolutionSection}, .owner = "original", .file = "evolution.yaml", .node = *node};
}

// The value of an optional the test requires. Without exceptions a failed REQUIRE ends the run, and so does this;
// the explicit check is what clang-tidy sees.
template <typename T>
const T& valueOf(const std::optional<T>& value) {
    if (!value) {
        FAIL_CHECK("a required value is missing");
        std::abort();
    }
    return *value;
}

// The game pack's evolution rules.
inline EvolutionRules originalRules() {
    auto rules = readEvolutionRules(sectionFrom(originalSectionText()));
    REQUIRE_MESSAGE(rules.has_value(), errorOf(rules));
    return *rules;
}

// The game pack's evolution rules with one quirk set.
inline EvolutionRules originalRulesWithQuirk(std::string_view quirk, bool on) {
    std::string text = originalSectionText();
    const std::string flag = std::format("{}: true", quirk);
    const std::size_t at = text.find(flag);
    REQUIRE_MESSAGE(at != std::string::npos, quirk);
    text.replace(at, flag.size(), std::format("{}: {}", quirk, on ? "true" : "false"));
    auto rules = readEvolutionRules(sectionFrom(text));
    REQUIRE_MESSAGE(rules.has_value(), errorOf(rules));
    return *rules;
}

// The rules with every special's roll always passing, or never.
inline EvolutionRules withRolls(EvolutionRules rules, bool pass) {
    for (auto* specials : {&rules.specials.wakeUp, &rules.specials.praiseScold}) {
        for (SpecialRule& special : *specials) {
            if (special.rollBelow) {
                special.rollBelow = pass ? rules.specials.rollLimit : 0;
            }
        }
    }
    return rules;
}

// A made-up form of the fixture tables: an id the original ruleset names, or one of the test's own.
struct FixtureForm {
    std::string_view id;
    std::int32_t level = 0;
};

// Every id the original section names, plus a few forms of the tests' own, with made-up indices and levels.
inline constexpr std::array<FixtureForm, 33> FixtureForms{{
    {.id = "boys", .level = 0}, {.id = "bota", .level = 1}, {.id = "puni", .level = 1}, {.id = "poyo", .level = 1},
    {.id = "yura", .level = 1}, {.id = "koro", .level = 2}, {.id = "tuno", .level = 2}, {.id = "toko", .level = 2},
    {.id = "tane", .level = 2}, {.id = "agum", .level = 3}, {.id = "gabu", .level = 3}, {.id = "kune", .level = 3},
    {.id = "grey", .level = 4}, {.id = "tyra", .level = 4}, {.id = "nume", .level = 4}, {.id = "aird", .level = 4},
    {.id = "bird", .level = 4}, {.id = "sead", .level = 4}, {.id = "igam", .level = 4}, {.id = "vegi", .level = 4},
    {.id = "mono", .level = 4}, {.id = "dori", .level = 4}, {.id = "sira", .level = 4}, {.id = "hoee", .level = 4},
    {.id = "shel", .level = 4}, {.id = "nani", .level = 4}, {.id = "scum", .level = 4}, {.id = "panj", .level = 4},
    {.id = "devi", .level = 4}, {.id = "mtgr", .level = 5}, {.id = "veda", .level = 5}, {.id = "ggdr", .level = 5},
    {.id = "mtet", .level = 5},
}};

inline constexpr std::int32_t FixtureMoveCount = 122;
inline constexpr std::int32_t FixtureDefaultWeight = 20;

// Made-up tables for the rules: every form of FixtureForms at its position, with no requirement, an empty path and
// reference stats of 1000 HP and MP and 100 of the rest. Tests change the asset tables and then build them.
class TableFixture {
public:
    TableFixture() {
        for (std::int32_t index = 0; index < FixtureMoveCount; ++index) {
            moves.push_back({.id = std::format("move_{}", index), .index = index, .power = 10 + index});
        }
        for (std::size_t position = 0; position < FixtureForms.size(); ++position) {
            const FixtureForm& form = FixtureForms.at(position);
            const auto index = static_cast<std::int32_t>(position);
            assets::DigimonRow entry;
            entry.id = form.id;
            entry.index = index;
            entry.name = form.id;
            entry.level = form.level;
            entry.special = {0, std::nullopt, std::nullopt};
            entry.moves.at(0) = "move_2";
            entry.moves.at(1) = "move_1";
            entry.moves.at(2) = "move_60";
            digimon.digimon.push_back(std::move(entry));
            assets::RaiseRow raised;
            raised.digimon = std::string{form.id};
            raised.index = index;
            raised.defaultWeight = FixtureDefaultWeight;
            raise.raise.push_back(std::move(raised));
            evolution.requirements.push_back({.target = std::string{form.id},
                                              .index = index,
                                              .digimon = std::nullopt,
                                              .hp = NoRequirement,
                                              .mp = NoRequirement,
                                              .offense = NoRequirement,
                                              .defense = NoRequirement,
                                              .speed = NoRequirement,
                                              .brain = NoRequirement,
                                              .care = NoRequirement,
                                              .weight = NoRequirement,
                                              .discipline = NoRequirement,
                                              .happiness = NoRequirement,
                                              .battles = NoRequirement,
                                              .techs = NoRequirement,
                                              .flags = 0});
            evolution.statsGains.push_back({.target = std::string{form.id},
                                            .index = index,
                                            .hp = 1000,
                                            .mp = 1000,
                                            .offense = 100,
                                            .defense = 100,
                                            .speed = 100,
                                            .brains = 100,
                                            .targetDigimon = std::string{form.id}});
            if (index > 0) {
                evolution.paths.push_back({.digimon = std::string{form.id}, .index = index - 1, .from = {}, .to = {}});
            }
        }
    }

    // The index of a fixture form.
    [[nodiscard]] static Species index(std::string_view id) {
        for (std::size_t position = 0; position < FixtureForms.size(); ++position) {
            if (FixtureForms.at(position).id == id) {
                return static_cast<Species>(position);
            }
        }
        FAIL("no fixture form " << id);
        return 0;
    }

    assets::DigimonRow& form(std::string_view id) { return digimon.digimon.at(static_cast<std::size_t>(index(id))); }

    assets::EvolutionRequirementEntry& requirement(std::string_view id) {
        return evolution.requirements.at(static_cast<std::size_t>(index(id)));
    }

    assets::EvolutionStatsGainEntry& gains(std::string_view id) {
        return evolution.statsGains.at(static_cast<std::size_t>(index(id)));
    }

    // Sets the targets of a form's chart, in order.
    void setPath(std::string_view from, std::initializer_list<std::string_view> targets) {
        auto& path = evolution.paths.at(static_cast<std::size_t>(index(from) - 1));
        path.to = {};
        std::size_t slot = 0;
        for (const std::string_view target : targets) {
            path.to.at(slot++) = std::string{target};
        }
    }

    [[nodiscard]] EvolutionTables build() const {
        auto tables = EvolutionTables::build(digimon, raise, assets::MoveTable{moves}, evolution, itemEffects);
        REQUIRE_MESSAGE(tables.has_value(), errorOf(tables));
        return *tables;
    }

    // The original rules on these tables, with the original strategies.
    [[nodiscard]] Evolution makeEvolution(EvolutionRules rules = originalRules()) const {
        auto built = Evolution::create(std::move(rules), build(), std::make_unique<OriginalSelection>(),
                                       std::make_unique<OriginalScoring>());
        REQUIRE_MESSAGE(built.has_value(), errorOf(built));
        return std::move(*built);
    }

    assets::DigimonTable digimon;
    assets::RaiseTable raise;
    std::vector<assets::MoveEntry> moves;
    assets::EvolutionTable evolution;
    assets::ItemEffectTable itemEffects;
};

// Inputs of a form with the given timer and nothing else, but the virus form the care section names in the
// original (care.toilet.virus_form).
inline EvolutionInputs inputsOf(std::string_view id, std::int32_t timer = 0) {
    return {.species = TableFixture::index(id), .evolutionTimer = timer, .virusForm = TableFixture::index("scum")};
}

} // namespace dwe::game::evolution::testing
