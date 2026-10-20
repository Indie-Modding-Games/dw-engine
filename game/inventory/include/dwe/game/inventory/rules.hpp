#pragma once

#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The numbers of the inventory: the `inventory` section of a ruleset (rules/<id>/inventory.yaml,
// content/schemas/ruleset_inventory.schema.json, docs/reference/inventory.md). Per-item data is not here: it comes
// from data/items and data/item_effects.
namespace dwe::game::inventory {

// The section's name in a ruleset.
inline constexpr std::string_view SectionName = "inventory";
// The path of the stacking strategy in the section.
inline constexpr std::string_view StackingPath = "bag.stacking";

// The categories of sorting_value (item.c: sortItems).
inline constexpr std::size_t SortCategoryCount = 6;

// The quirk flags of the section.
inline constexpr std::string_view StackOverflowIsLost = "stack_overflow_is_lost";
inline constexpr std::string_view BabyDiscountLimitedByFullPrice = "baby_discount_limited_by_full_price";
inline constexpr std::string_view OmniDiskIgnoresLimit = "omni_disk_ignores_limit";
inline constexpr std::string_view UselessItemsConsumed = "useless_items_consumed";

struct BagRules {
    std::int32_t maxSlots = 0;
    // The sizes the story can give the bag, smallest first.
    std::vector<std::int32_t> slotCounts;
    std::int32_t newGameSlots = 0;
    std::int32_t newGameMoney = 0;
    std::int32_t maxStack = 0;
};

struct UseRules {
    // The item_color values usable in each context.
    std::vector<std::int32_t> fieldColors;
    std::vector<std::int32_t> battleColors;
    // The droppable value that lets an item be thrown away.
    std::int32_t discardDroppable = 0;
    // The droppable value of the key items, which can be neither sold nor thrown away.
    std::int32_t keyItemDroppable = 0;
};

struct ShopRules {
    std::int32_t stockTriggerBase = 0;
    std::int32_t sellDivisor = 1;
    std::int32_t babyDiscountPercent = 0;
    std::int32_t maxAmount = 0;
};

// What a status item does (item.c: handleStatusItems).
struct StatusItemRule {
    std::int32_t item = 0;
    std::optional<std::int32_t> battleStatusCure = std::nullopt;
    std::int32_t hp = 0;
    std::int32_t mp = 0;
    bool protects = false;
    // The rolls under which handleMedicineHealing cures; 0 cures nothing.
    std::int32_t injuryCure = 0;
    std::int32_t sicknessCure = 0;
};

struct ReviveItemRule {
    std::int32_t item = 0;
    std::int32_t hpPercent = 0;
    std::optional<std::int32_t> battleStatusCure = std::nullopt;
};

struct BattleDiskRule {
    std::int32_t item = 0;
    std::int32_t offense = 0;
    std::int32_t defense = 0;
    std::int32_t speed = 0;
    // The disk that raises all three (main.c: handleBuffDisks case 0x12).
    bool omni = false;
};

struct ScriptItemRule {
    std::int32_t item = 0;
    std::int32_t section = 0;
};

struct TamerLevelRule {
    std::vector<std::int32_t> items;
    std::int32_t chancePercent = 0;
    std::int32_t amount = 0;
};

struct FixedEvolutionRule {
    std::int32_t item = 0;
    std::string evolvesTo;
};

// The logic constants of the item functions; the per-item numbers are in data/item_effects.
struct EffectRules {
    std::int32_t statLimit = 0;
    std::int32_t hpMpLimit = 0;
    std::vector<assets::ItemHandler> consciousOnly;
    std::int32_t doubleFloppyHp = 0;
    std::int32_t doubleFloppyMp = 0;
    std::vector<StatusItemRule> status;
    std::int32_t injuryRoll = 0;
    std::int32_t sicknessRoll = 0;
    std::vector<ReviveItemRule> revive;
    std::int32_t diskLimitPercent = 0;
    std::int32_t diskLimitCap = 0;
    std::vector<BattleDiskRule> disks;
    std::vector<ScriptItemRule> scriptChips;
    std::vector<std::int32_t> toiletChips;
    TamerLevelRule tamerLevel{};
    std::int32_t favouriteEnergyPercent = 0;
    std::int32_t favouriteHappiness = 0;
    std::int32_t poopLevelChange = 0;
    bool evolutionRequiresPreviousLevel = false;
    std::vector<FixedEvolutionRule> fixedEvolutions;
};

struct InventoryQuirks {
    bool stackOverflowIsLost = false;
    bool babyDiscountLimitedByFullPrice = false;
    bool omniDiskIgnoresLimit = false;
    bool uselessItemsConsumed = false;
};

struct InventoryRules {
    BagRules bag{};
    std::int32_t maxMoney = 0;
    // The category order of each sort mode (inventory.c: ITEM_SORT_ORDER).
    std::vector<std::array<std::int32_t, SortCategoryCount>> sortOrders;
    UseRules use{};
    std::int32_t defeatLostPercent = 0;
    ShopRules shop{};
    EffectRules effects{};
    InventoryQuirks quirks{};
};

// Reads the `inventory` section. Unknown and missing keys and values out of range are errors at their line.
core::Result<InventoryRules> parseInventoryRules(const ruleset::Section& section);

// Whether the item is a key item: its droppable value is the ruleset's key_item_droppable.
bool isKeyItem(const assets::ItemEntry& item, const UseRules& rules);

// The section's check for loadRuleset and RulesetStore: it parses the section and checks the stacking strategy.
ruleset::SectionCheck inventorySectionCheck();

} // namespace dwe::game::inventory
