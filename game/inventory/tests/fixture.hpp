#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/inventory/item_effects.hpp>
#include <dwe/game/inventory/rules.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The game pack's original ruleset, over a stand-in base pack with empty tables, and made-up items: no item of the
// original is copied here. The item numbers are those the ruleset's rules name (status 8 to 14, disks 15 to 21,
// chips 22 to 37, evolution 125 to 127), so the rules apply to them.
namespace dwe::game::inventory::testing {

template <typename T>
std::string errorOf(const core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// The value of an optional the test needs; after a missing one fails the test, a default one.
template <typename T>
T required(const std::optional<T>& value) {
    REQUIRE(value.has_value());
    return value.value_or(T{});
}

inline constexpr std::array<std::string_view, 8> TableRoles{"digimon", "evolution", "raise",        "training",
                                                            "moves",   "items",     "item_effects", "shops"};

// The made-up items.
inline constexpr std::int32_t Tonic = 0;       // hp_healing, field and battle, 100 bits
inline constexpr std::int32_t Ether = 4;       // mp_healing, battle only
inline constexpr std::int32_t Floppy = 7;      // double_floppy
inline constexpr std::int32_t Bandage = 13;    // status: medicine
inline constexpr std::int32_t Revival = 12;    // revive
inline constexpr std::int32_t PowerDisk = 15;  // battle disk
inline constexpr std::int32_t OmniDisk = 18;   // battle disk raising the three
inline constexpr std::int32_t ScriptChip = 22; // runs a script section
inline constexpr std::int32_t BrainChip = 25;  // chip
inline constexpr std::int32_t DoubleChip = 29; // chip that may lower the tamer level
inline constexpr std::int32_t Toilet = 32;     // relieves the partner
inline constexpr std::int32_t Charm = 33;      // a chip whose case does nothing
inline constexpr std::int32_t Bun = 38;        // food
inline constexpr std::int32_t Steak = 39;      // food, sorting category 1
inline constexpr std::int32_t Crest = 71;      // evolution by table
inline constexpr std::int32_t BigCrest = 125;  // evolution fixed by the ruleset
inline constexpr std::int32_t Rod = 116;       // no handler, field
inline constexpr std::int32_t GateKey = 123;   // no handler, never usable, cannot be thrown away
inline constexpr std::int32_t Hidden = 50;     // food the extractor could not decode

struct FixtureItem {
    std::int32_t index;
    std::int32_t value;
    std::int32_t sortingValue;
    std::int32_t itemColor;
    std::int32_t droppable;
};

inline assets::ItemTable fixtureItems() {
    constexpr std::array<FixtureItem, 19> Items{{
        {.index = Tonic, .value = 100, .sortingValue = 0, .itemColor = 2, .droppable = 1},
        {.index = Ether, .value = 300, .sortingValue = 0, .itemColor = 1, .droppable = 1},
        {.index = Floppy, .value = 2000, .sortingValue = 0, .itemColor = 2, .droppable = 1},
        {.index = Bandage, .value = 50, .sortingValue = 2, .itemColor = 0, .droppable = 1},
        {.index = Revival, .value = 3000, .sortingValue = 0, .itemColor = 2, .droppable = 1},
        {.index = PowerDisk, .value = 500, .sortingValue = 3, .itemColor = 1, .droppable = 1},
        {.index = OmniDisk, .value = 900, .sortingValue = 3, .itemColor = 1, .droppable = 1},
        {.index = ScriptChip, .value = 0, .sortingValue = 4, .itemColor = 0, .droppable = 1},
        {.index = BrainChip, .value = 4000, .sortingValue = 4, .itemColor = 0, .droppable = 1},
        {.index = DoubleChip, .value = 9000, .sortingValue = 4, .itemColor = 0, .droppable = 1},
        {.index = Toilet, .value = 800, .sortingValue = 2, .itemColor = 0, .droppable = 1},
        {.index = Charm, .value = 1000, .sortingValue = 5, .itemColor = 0, .droppable = 1},
        {.index = Bun, .value = 30, .sortingValue = 1, .itemColor = 0, .droppable = 1},
        {.index = Steak, .value = 120, .sortingValue = 1, .itemColor = 0, .droppable = 1},
        {.index = Crest, .value = 0, .sortingValue = 4, .itemColor = 0, .droppable = 1},
        {.index = BigCrest, .value = 0, .sortingValue = 4, .itemColor = 0, .droppable = 1},
        {.index = Rod, .value = 100, .sortingValue = 5, .itemColor = 0, .droppable = 0},
        {.index = GateKey, .value = 100, .sortingValue = 5, .itemColor = 255, .droppable = 0},
        {.index = Hidden, .value = 10, .sortingValue = 1, .itemColor = 0, .droppable = 1},
    }};
    assets::ItemTable table;
    for (const FixtureItem& item : Items) {
        table.items.push_back({.id = std::format("item_{}", item.index),
                               .index = item.index,
                               .value = item.value,
                               .meritValue = 0,
                               .sortingValue = item.sortingValue,
                               .itemColor = item.itemColor,
                               .droppable = item.droppable});
    }
    return table;
}

inline assets::ItemEffectEntry effectEntry(std::int32_t index, assets::ItemHandler handler) {
    return {.item = std::format("item_{}", index), .index = index, .handler = handler};
}

inline assets::ItemEffectTable fixtureEffects() {
    using assets::ItemHandler;
    assets::ItemEffectTable table;
    auto tonic = effectEntry(Tonic, ItemHandler::HpHealing);
    tonic.healing = 500;
    auto ether = effectEntry(Ether, ItemHandler::MpHealing);
    ether.healing = 400;
    auto brain = effectEntry(BrainChip, ItemHandler::Chip);
    brain.chip = assets::ChipValues{.brain = 50};
    auto twice = effectEntry(DoubleChip, ItemHandler::Chip);
    twice.chip = assets::ChipValues{.offense = 100, .brain = 100, .lifetime = -24};
    auto charm = effectEntry(Charm, ItemHandler::Chip);
    charm.chip = assets::ChipValues{};
    auto bun = effectEntry(Bun, ItemHandler::Food);
    bun.food = assets::FoodValues{.energy = 12, .weight = 1};
    auto steak = effectEntry(Steak, ItemHandler::Food);
    steak.food = assets::FoodValues{.energy = 20,
                                    .happiness = 3,
                                    .weight = 2,
                                    .trainingBoostFlag = 1,
                                    .trainingBoostValue = 2,
                                    .trainingBoostHours = 4,
                                    .sicknessChance = 5};
    auto crest = effectEntry(Crest, ItemHandler::Evolution);
    crest.evolvesTo = "drake";
    table.effects = {tonic,
                     ether,
                     effectEntry(Floppy, ItemHandler::DoubleFloppy),
                     effectEntry(Bandage, ItemHandler::Status),
                     effectEntry(Revival, ItemHandler::Revive),
                     effectEntry(PowerDisk, ItemHandler::BattleDisk),
                     effectEntry(OmniDisk, ItemHandler::BattleDisk),
                     effectEntry(ScriptChip, ItemHandler::Chip),
                     brain,
                     twice,
                     effectEntry(Toilet, ItemHandler::Chip),
                     charm,
                     bun,
                     steak,
                     crest,
                     effectEntry(BigCrest, ItemHandler::Evolution),
                     effectEntry(Rod, ItemHandler::None),
                     effectEntry(GateKey, ItemHandler::None),
                     effectEntry(Hidden, ItemHandler::Food)};
    return table;
}

// The original ruleset of the game pack, mounted over the stand-in base pack, its inventory section checked.
inline ruleset::Ruleset originalRuleset() {
    assets::MemoryFileSystem memory;
    memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
    for (const std::string_view role : TableRoles) {
        memory.addFile(std::format("base/data/{}.yaml", role), std::format("{}: []\n", role));
    }
    assets::DiskFileSystem disk;
    const std::array<assets::PackLocation, 2> packs{
        assets::PackLocation{
            .fileSystem = &memory, .root = "base", .fillsGaps = false, .origin = assets::PackOrigin::Engine},
        assets::PackLocation{
            .fileSystem = &disk, .root = DWE_GAME_PACK, .fillsGaps = false, .origin = assets::PackOrigin::Engine}};
    auto vfs = assets::VirtualFileSystem::mount(packs);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    const ruleset::SectionChecks checks{{std::string{SectionName}, inventorySectionCheck()}};
    auto loaded = ruleset::loadRuleset(*vfs, ruleset::OriginalRuleset, checks);
    REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
    return std::move(*loaded);
}

inline Inventory originalInventory() {
    auto inventory = Inventory::fromRuleset(originalRuleset());
    REQUIRE_MESSAGE(inventory.has_value(), errorOf(inventory));
    return std::move(*inventory);
}

inline InventoryRules originalRules() {
    return originalInventory().rules();
}

inline ItemEffects fixtureItemEffects(const InventoryRules& rules) {
    return {fixtureEffects(), rules.effects, rules.quirks};
}

} // namespace dwe::game::inventory::testing
