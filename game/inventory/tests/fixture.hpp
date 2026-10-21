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

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The game pack's original ruleset, over a stand-in base pack with empty tables, and made-up items: no item's data
// of the original is copied here. The items the ruleset's rules name (a status item, a revive, the disks, the chips, a
// fixed evolution) have the id it names, so the rules apply to them, and the items its ranges count have numbers in
// them.
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

// The made-up items, by id. Those the original ruleset names keep the id it names them by (and the number of the
// original, which its ranges count); the rest have made-up ids.
struct FixtureItem {
    std::string_view id;
    std::int32_t index;
    std::int32_t value;
    std::int32_t sortingValue;
    std::int32_t itemColor;
    std::int32_t droppable;
};

inline constexpr std::string_view Tonic = "tonic";           // hp_healing, field and battle, 100 bits
inline constexpr std::string_view Ether = "ether";           // mp_healing, battle only
inline constexpr std::string_view Floppy = "floppy";         // double_floppy
inline constexpr std::string_view Bandage = "bandage";       // status: medicine
inline constexpr std::string_view Revival = "sup_restore";   // revive
inline constexpr std::string_view PowerDisk = "off_disk";    // battle disk
inline constexpr std::string_view OmniDisk = "omni_disk";    // battle disk raising the three
inline constexpr std::string_view ScriptChip = "auto_pilot"; // runs a script section
inline constexpr std::string_view BrainChip = "brain";       // chip
inline constexpr std::string_view DoubleChip = "dv_chip_a";  // chip that may lower the tamer level
inline constexpr std::string_view Toilet = "port_potty";     // relieves the partner
inline constexpr std::string_view Charm = "charm";           // a chip whose case does nothing
inline constexpr std::string_view Bun = "bun";               // food
inline constexpr std::string_view Steak = "steak";           // food, sorting category 1
inline constexpr std::string_view Crest = "crest";           // evolution by table
inline constexpr std::string_view BigCrest = "giga_hand";    // evolution fixed by the ruleset
inline constexpr std::string_view Rod = "rod";               // no handler, field
inline constexpr std::string_view GateKey = "gate_key";      // no handler, never usable, cannot be thrown away
inline constexpr std::string_view Hidden = "hidden";         // food the extractor could not decode

inline constexpr std::array<FixtureItem, 19> FixtureItems{{
    {.id = Tonic, .index = 0, .value = 100, .sortingValue = 0, .itemColor = 2, .droppable = 1},
    {.id = Ether, .index = 4, .value = 300, .sortingValue = 0, .itemColor = 1, .droppable = 1},
    {.id = Floppy, .index = 7, .value = 2000, .sortingValue = 0, .itemColor = 2, .droppable = 1},
    {.id = Bandage, .index = 13, .value = 50, .sortingValue = 2, .itemColor = 0, .droppable = 1},
    {.id = Revival, .index = 12, .value = 3000, .sortingValue = 0, .itemColor = 2, .droppable = 1},
    {.id = PowerDisk, .index = 15, .value = 500, .sortingValue = 3, .itemColor = 1, .droppable = 1},
    {.id = OmniDisk, .index = 18, .value = 900, .sortingValue = 3, .itemColor = 1, .droppable = 1},
    {.id = ScriptChip, .index = 22, .value = 0, .sortingValue = 4, .itemColor = 0, .droppable = 1},
    {.id = BrainChip, .index = 25, .value = 4000, .sortingValue = 4, .itemColor = 0, .droppable = 1},
    {.id = DoubleChip, .index = 29, .value = 9000, .sortingValue = 4, .itemColor = 0, .droppable = 1},
    {.id = Toilet, .index = 32, .value = 800, .sortingValue = 2, .itemColor = 0, .droppable = 1},
    {.id = Charm, .index = 33, .value = 1000, .sortingValue = 5, .itemColor = 0, .droppable = 1},
    {.id = Bun, .index = 38, .value = 30, .sortingValue = 1, .itemColor = 0, .droppable = 1},
    {.id = Steak, .index = 39, .value = 120, .sortingValue = 1, .itemColor = 0, .droppable = 1},
    {.id = Crest, .index = 71, .value = 0, .sortingValue = 4, .itemColor = 0, .droppable = 1},
    {.id = BigCrest, .index = 125, .value = 0, .sortingValue = 4, .itemColor = 0, .droppable = 1},
    {.id = Rod, .index = 116, .value = 100, .sortingValue = 5, .itemColor = 0, .droppable = 0},
    {.id = GateKey, .index = 123, .value = 100, .sortingValue = 5, .itemColor = 255, .droppable = 0},
    {.id = Hidden, .index = 50, .value = 10, .sortingValue = 1, .itemColor = 0, .droppable = 1},
}};

// The other items the original ruleset names, with the number its ranges count: plain items, so that every id the
// ruleset names is in the table (checkItemIds).
struct RulesetItem {
    std::string_view id;
    std::int32_t index;
};

inline constexpr std::array<RulesetItem, 14> RulesetItems{{
    {.id = "various", .index = 8},
    {.id = "omnipotent", .index = 9},
    {.id = "protection", .index = 10},
    {.id = "restore", .index = 11},
    {.id = "medicine", .index = 14},
    {.id = "def_disk", .index = 16},
    {.id = "hispeed_dsk", .index = 17},
    {.id = "s_off_disk", .index = 19},
    {.id = "s_def_disk", .index = 20},
    {.id = "s_speed_disk", .index = 21},
    {.id = "dv_chip_d", .index = 30},
    {.id = "dv_chip_e", .index = 31},
    {.id = "noble_mane", .index = 126},
    {.id = "metalbanana", .index = 127},
}};

// The original has this many items: a plain item (`item_<n>`) stands in for each number no other item here has, so
// the ranges of the original ruleset (item_use) cover items, as they do on the original's table.
inline constexpr std::int32_t OriginalItemCount = 128;

// Plain items past the original's (`filler_<n>`, numbers from FillerBase), to fill a bag.
inline constexpr std::int32_t FillerCount = 30;
inline constexpr std::int32_t FillerBase = 200;

// The number of the original a made-up item has.
inline std::int32_t numberOf(std::string_view id) {
    for (const FixtureItem& item : FixtureItems) {
        if (item.id == id) {
            return item.index;
        }
    }
    FAIL("no made-up item ", id);
    return -1;
}

// The item table of the tests: one composition for every test, so its handles are good in all of them.
inline const assets::ItemTable& fixtureItems() {
    static const assets::ItemTable table = [] {
        std::vector<assets::ItemEntry> entries;
        entries.reserve(FixtureItems.size() + RulesetItems.size() +
                        static_cast<std::size_t>(OriginalItemCount + FillerCount));
        for (const FixtureItem& item : FixtureItems) {
            entries.push_back({.id = std::string{item.id},
                               .index = item.index,
                               .value = item.value,
                               .meritValue = 0,
                               .sortingValue = item.sortingValue,
                               .itemColor = item.itemColor,
                               .droppable = item.droppable});
        }
        for (const RulesetItem& item : RulesetItems) {
            entries.push_back({.id = std::string{item.id},
                               .index = item.index,
                               .value = 10,
                               .meritValue = 0,
                               .sortingValue = 5,
                               .itemColor = 0,
                               .droppable = 1});
        }
        for (std::int32_t number = 0; number < OriginalItemCount; ++number) {
            if (std::ranges::none_of(entries, [&](const assets::ItemEntry& entry) { return entry.index == number; })) {
                entries.push_back({.id = std::format("item_{}", number),
                                   .index = number,
                                   .value = 10,
                                   .meritValue = 0,
                                   .sortingValue = 5,
                                   .itemColor = 0,
                                   .droppable = 1});
            }
        }
        for (std::int32_t filler = 0; filler < FillerCount; ++filler) {
            entries.push_back({.id = std::format("filler_{}", filler),
                               .index = FillerBase + filler,
                               .value = 10,
                               .meritValue = 0,
                               .sortingValue = 5,
                               .itemColor = 0,
                               .droppable = 1});
        }
        return assets::ItemTable{std::move(entries)};
    }();
    return table;
}

// The handle of a made-up item in fixtureItems.
inline assets::ItemHandle handleOf(std::string_view id) {
    const std::optional<assets::ItemHandle> handle = fixtureItems().handleOf(id);
    REQUIRE_MESSAGE(handle.has_value(), "no made-up item ", id);
    return handle.value_or(fixtureItems().handleOf(fixtureItems().entries().front()));
}

// The handle of a filler item.
inline assets::ItemHandle filler(std::int32_t number) {
    return handleOf(std::format("filler_{}", number));
}

// The id of the item a handle of fixtureItems names.
inline std::string idOf(assets::ItemHandle handle) {
    return fixtureItems().at(handle).id;
}

inline assets::ItemEffectEntry effectEntry(std::string_view id, assets::ItemHandler handler) {
    return {.item = std::string{id}, .index = numberOf(id), .handler = handler};
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
