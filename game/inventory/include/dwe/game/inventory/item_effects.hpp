#pragma once

#include <dwe/assets/item_effect_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/inventory/item_effect.hpp>
#include <dwe/game/inventory/rules.hpp>

#include <cstdint>

namespace dwe::game::inventory {

// Resolves what an item does: the handler and numbers of data/item_effects, completed with the ruleset's logic
// constants for the handlers whose numbers are code (status items, revives, disks, special chips, evolutions).
class ItemEffects {
public:
    ItemEffects(assets::ItemEffectTable table, EffectRules rules, InventoryQuirks quirks);

    // The effect of an item; an error names an item whose effect neither the table nor the ruleset gives.
    [[nodiscard]] core::Result<ItemEffect> resolve(std::int32_t item) const;

private:
    [[nodiscard]] core::Result<void> describe(const assets::ItemEffectEntry& entry, ItemEffect& effect) const;
    [[nodiscard]] core::Result<void> describeStatus(std::int32_t item, ItemEffect& effect) const;
    [[nodiscard]] core::Result<void> describeRevive(std::int32_t item, ItemEffect& effect) const;
    [[nodiscard]] core::Result<void> describeDisk(std::int32_t item, ItemEffect& effect) const;
    [[nodiscard]] core::Result<void> describeChip(const assets::ItemEffectEntry& entry, ItemEffect& effect) const;
    [[nodiscard]] core::Result<void> describeFood(const assets::ItemEffectEntry& entry, ItemEffect& effect) const;
    [[nodiscard]] core::Result<void> describeEvolution(const assets::ItemEffectEntry& entry, ItemEffect& effect) const;

    assets::ItemEffectTable table_;
    EffectRules rules_;
    InventoryQuirks quirks_;
};

} // namespace dwe::game::inventory
