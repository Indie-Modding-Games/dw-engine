#pragma once

#include <dwe/assets/entry_class.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::session {

// The section of how the partner takes the items the tamer gives it (rules/<id>/item_use.yaml).
inline constexpr std::string_view ItemUseSection = "item_use";

// An evolution item the partner takes only at one level of data/digimon.
struct FixedLevelEvolution {
    // Its id in data/items.
    std::string item;
    std::int32_t level = 0;
};

// Which items the partner refuses (partner_impl.c: partnerWillRefuseItem).
// The classes of items are ranges of the original's numbers and ids (assets::EntryClass): a pack's item joins one by
// its id.
struct ItemRefusalRules {
    assets::EntryClass neverTaken;
    // Items with a lower original number, and those named, go through the discipline roll; the rest are refused only
    // as unwanted favourite food.
    assets::EntryClass disciplineChecked;
    std::vector<FixedLevelEvolution> fixedLevelEvolutions;
    // The other evolution items are refused when their target's level minus the partner's is one of these.
    std::vector<std::int32_t> refusedLevelDifferences;
};

struct ItemUseRules {
    ItemRefusalRules refusal;
    // The items that answer hunger (partner_impl.c: partnerHandleFoodFeed).
    assets::EntryClass meals;
    // The partner levels of data/digimon that get the shops' baby discount (script_common.c: isPartnerBaby).
    std::vector<std::int32_t> shopDiscountLevels;
    // The bit of each stat in a food's training boost flag (trn_reward.c), by game::partner::Stat.
    game::partner::StatBlock trainingBoostBits;
};

// Reads the section; errors name the file, line and column.
core::Result<ItemUseRules> readItemUseRules(const ruleset::Section& section);

// Checks the rules against the session's data/items: the ranges cover items of the original and the ids are items.
core::Result<void> checkItemUseItems(const ItemUseRules& rules, const assets::ItemTable& items);

// The section's check for the ruleset store.
core::Result<void> checkItemUseSection(const ruleset::Section& section);

} // namespace dwe::session
