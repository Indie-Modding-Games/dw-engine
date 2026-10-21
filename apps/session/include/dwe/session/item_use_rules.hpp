#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace dwe::session {

// The section of how the partner takes the items the tamer gives it (rules/<id>/item_use.yaml).
inline constexpr std::string_view ItemUseSection = "item_use";

// Items from `from` to `to`, both included, by their number in data/items.
struct ItemRange {
    std::int32_t from = 0;
    std::int32_t to = 0;

    [[nodiscard]] bool contains(std::int32_t item) const { return item >= from && item <= to; }
};

// An evolution item the partner takes only at one level of data/digimon.
struct FixedLevelEvolution {
    std::int32_t item = 0;
    std::int32_t level = 0;
};

// Which items the partner refuses (partner_impl.c: partnerWillRefuseItem).
struct ItemRefusalRules {
    std::vector<ItemRange> neverTaken;
    // Items with a lower number go through the discipline roll; the rest are refused only as unwanted favourite food.
    std::int32_t disciplineCheckedBelow = 0;
    std::vector<FixedLevelEvolution> fixedLevelEvolutions;
    // The other evolution items are refused when their target's level minus the partner's is one of these.
    std::vector<std::int32_t> refusedLevelDifferences;
};

struct ItemUseRules {
    ItemRefusalRules refusal;
    // The items that answer hunger (partner_impl.c: partnerHandleFoodFeed).
    std::vector<ItemRange> meals;
    // The partner levels of data/digimon that get the shops' baby discount (script_common.c: isPartnerBaby).
    std::vector<std::int32_t> shopDiscountLevels;
    // The bit of each stat in a food's training boost flag (trn_reward.c), by game::partner::Stat.
    game::partner::StatBlock trainingBoostBits;
};

// Whether one of the ranges holds the item.
[[nodiscard]] bool inRanges(const std::vector<ItemRange>& ranges, std::int32_t item);

// Reads the section; errors name the file, line and column.
core::Result<ItemUseRules> readItemUseRules(const ruleset::Section& section);

// The section's check for the ruleset store.
core::Result<void> checkItemUseSection(const ruleset::Section& section);

} // namespace dwe::session
