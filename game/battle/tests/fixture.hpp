#pragma once

// Shared helpers of the battle tests: the original battle section from the game pack, a small made-up moves table
// with the moves the section names, and fighters to put in a battle.

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace battle_test {

using namespace dwe::game::battle;

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

inline std::string originalSectionText() {
    const auto bytes = dwe::assets::DiskFileSystem{}.readFile(std::filesystem::path{DWE_GAME_PACK} / "rules" /
                                                              "original" / "battle.yaml");
    REQUIRE(bytes);
    return {reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

inline dwe::ruleset::Section sectionOf(std::string_view yaml) {
    auto node = dwe::core::parseYaml(yaml);
    REQUIRE_MESSAGE(node, errorOf(node));
    return {.name = std::string{SectionName}, .owner = "test", .file = "rules/test/battle.yaml", .node = *node};
}

// The original section with one line replaced: `from` must be in it.
inline std::string originalWith(std::string_view from, std::string_view to) {
    std::string text = originalSectionText();
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

inline BattleRules rulesOf(std::string_view yaml) {
    auto rules = readBattleRules(sectionOf(yaml));
    REQUIRE_MESSAGE(rules, errorOf(rules));
    return std::move(*rules);
}

inline const BattleRules& originalRules() {
    static const BattleRules rules = rulesOf(originalSectionText());
    return rules;
}

// A made-up move: numbers chosen for the tests, not the original's.
inline dwe::assets::MoveEntry testMove(std::string id, std::int32_t index, std::int32_t range, std::int32_t power,
                                       std::int32_t mpCost, std::int32_t distance, std::int32_t special,
                                       std::int32_t status = 0, std::int32_t statusChance = 0, std::int32_t flags = 1) {
    dwe::assets::MoveEntry entry;
    entry.id = std::move(id);
    entry.index = index;
    entry.name = entry.id;
    entry.distance = distance;
    entry.power = power;
    entry.mpCost = mpCost;
    entry.iframes = 6;
    entry.range = range;
    entry.special = special;
    entry.status = status;
    entry.accuracy = 100;
    entry.statusChance = statusChance;
    entry.flags = flags;
    return entry;
}

constexpr std::int32_t Melee = 1;
constexpr std::int32_t Ranged = 2;
constexpr std::int32_t Area = 3;
constexpr std::int32_t Self = 4;
constexpr std::int32_t Near = 250000;

inline dwe::assets::MoveTable testMoveTable() {
    std::vector<dwe::assets::MoveEntry> moves = {
        testMove("punch", 0, Melee, 30, 5, 0, 1),
        testMove("shot", 1, Ranged, 40, 10, Near, 0),
        testMove("poison_bite", 2, Melee, 20, 5, 0, 3, 1, 100),
        testMove("dizzy_ray", 3, Ranged, 20, 5, Near, 2, 2, 100),
        testMove("stun_slap", 4, Melee, 20, 5, 0, 1, 3, 100),
        testMove("squash", 5, Melee, 20, 5, 0, 6, 4, 100),
        testMove("wide_blast", 6, Area, 35, 20, Near, 4),
        testMove("aqua_magic", 21, Self, 0, 10, 0, 0, 0, 0, 0),
        testMove("full_potential", 30, Self, 0, 10, 0, 0, 0, 0, 0),
        testMove("mass_morph", 34, Self, 0, 10, 0, 0, 0, 0, 0),
        testMove("muscle_charge", 41, Self, 0, 10, 0, 0, 0, 0, 0),
        testMove("war_cry", 42, Self, 0, 10, 0, 0, 0, 0, 0),
        testMove("dynamite_kick", 44, Melee, 50, 15, 0, 1),
        testMove("counter", 45, Melee, 40, 0, 0, 1),
        testMove("dynamite_kick_48", 48, Melee, 50, 15, 0, 1),
        testMove("horizontal_kick", 55, Melee, 45, 12, 0, 1),
        testMove("horizontal_kick_57", 57, Melee, 45, 12, 0, 1),
        testMove("big_finish", 60, Melee, 200, 0, 0, 1),
        testMove("move_121", 121, Ranged, 10, 0, Near, 0),
    };
    moves.at(12).learnChances = dwe::assets::LearnChances{100, 100, 100};
    return dwe::assets::MoveTable{std::move(moves)};
}

inline const MoveCatalog& testCatalog() {
    static const MoveCatalog catalog = [] {
        auto built = MoveCatalog::build(testMoveTable(), originalRules());
        REQUIRE_MESSAGE(built, errorOf(built));
        return std::move(*built);
    }();
    return catalog;
}

inline CombatStats evenStats(std::int32_t hp, std::int32_t value) {
    return {.maxHp = hp, .maxMp = 500, .offense = value, .defense = value, .speed = value, .brain = value};
}

// A fighter with the given moves (empty strings leave a slot empty) at a point.
inline Combatant fighterWith(std::string species, CombatStats stats, std::vector<std::string> moves, Point at) {
    Combatant combatant;
    combatant.species = std::move(species);
    combatant.level = 3;
    combatant.type = DigimonType::Vaccine;
    combatant.specials = {1, std::nullopt, std::nullopt};
    combatant.radius = 100;
    combatant.stats = stats;
    combatant.hp = stats.maxHp;
    combatant.mp = stats.maxMp;
    for (std::size_t slot = 0; slot < moves.size() && slot < EquippedSlots; ++slot) {
        if (!moves[slot].empty()) {
            combatant.moves.at(slot) = moves[slot];
        }
    }
    combatant.movePriorities = {10, 10, 10, 10};
    combatant.position = at;
    combatant.bits = 50;
    return combatant;
}

// A partner against one enemy, a short walk apart.
inline BattleSetup duel(CombatStats partner, CombatStats enemy, std::vector<std::string> partnerMoves = {"punch"},
                        std::vector<std::string> enemyMoves = {"punch"}) {
    BattleSetup setup;
    setup.partner = fighterWith("hero", partner, std::move(partnerMoves), {.x = 0, .z = 0});
    setup.enemies.push_back(fighterWith("foe", enemy, std::move(enemyMoves), {.x = 0, .z = 600}));
    setup.condition.discipline = 50;
    setup.tamer = {.x = 0, .z = -800};
    return setup;
}

inline Battle startBattle(BattleSetup setup, std::uint64_t seed, const BattleRules& rules = originalRules()) {
    auto battle = Battle::start(std::move(setup), rules, testCatalog(), originalStrategies(), seed);
    REQUIRE_MESSAGE(battle, errorOf(battle));
    return std::move(*battle);
}

// Steps until the battle ends or the frame limit; returns every event.
inline BattleEvents runToEnd(Battle& battle, std::int64_t limit = 20000) {
    BattleEvents all;
    while (!battle.finished() && battle.state().frame < limit) {
        const BattleEvents& frame = battle.step();
        all.insert(all.end(), frame.begin(), frame.end());
    }
    return all;
}

// The result of a finished battle; fails the test if it has none.
inline const BattleResult& resultOf(const Battle& battle) {
    static const BattleResult none;
    const std::optional<BattleResult>& result = battle.result();
    REQUIRE(result.has_value());
    return result ? *result : none;
}

// The reference of a move of a catalog; fails the test if it has none.
inline MoveRef refOf(const MoveCatalog& moves, std::string_view id) {
    const std::optional<MoveRef> found = moves.find(id);
    REQUIRE(found.has_value());
    return found.value_or(MoveRef{});
}

inline bool hasEvent(const BattleEvents& events, EventKind kind) {
    return std::ranges::any_of(events, [kind](const BattleEvent& event) { return event.kind == kind; });
}

} // namespace battle_test
