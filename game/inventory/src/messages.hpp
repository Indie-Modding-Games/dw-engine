#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::game::inventory::messages {

using core::formatMessage;

// The error messages of the inventory, as std::format templates for core::formatMessage.

// The ruleset section.
inline constexpr std::string_view NotPositive = "the value must be more than zero";
inline constexpr std::string_view Negative = "the value cannot be negative";
inline constexpr std::string_view SlotCountsOrder = "slot counts must grow and end at max_slots";
inline constexpr std::string_view NotASlotCount = "{} is not one of slot_counts";
inline constexpr std::string_view StartingCountRange = "a new game's item count must be from 1 to max_stack ({})";
inline constexpr std::string_view StartingItemText = "a new game's item";
inline constexpr std::string_view SortOrderNotPermutation = "a sort order must list each category 0 to 5 once";
inline constexpr std::string_view UnknownHandler = "unknown item handler '{}'";
inline constexpr std::string_view PercentRange = "the value must be from 0 to 100";
inline constexpr std::string_view RuleItemTwice = "item {} has two rules";
inline constexpr std::string_view DiskStatsCount = "a disk names offense, defense and speed";

// The bag.
inline constexpr std::string_view SlotOutOfRange = "slot {} is outside the bag's {} slots";
inline constexpr std::string_view EmptySlot = "slot {} is empty";
inline constexpr std::string_view NotDiscardable = "item '{}' cannot be thrown away";
inline constexpr std::string_view DiscardOnlyInField = "items are thrown away only in the field";
inline constexpr std::string_view UnknownSortMode = "sort mode {} is not one of the {} orders";
inline constexpr std::string_view RuleItemUnknown = "inventory rules: {} names item '{}', which is not in data/items";
inline constexpr std::string_view BadCount = "a count must be from 1 to {}";
inline constexpr std::string_view SlotCountTooSmall = "the bag cannot shrink to {} slots: slot {} holds an item";

// The save.
inline constexpr std::string_view SlotText = "a slot";
inline constexpr std::string_view OldInventoryShape = "an inventory of version 1 has a list of slots";
inline constexpr std::string_view OldItemNumberInvalid =
    "an inventory of version 1 names item '{}', which is no number";

// Effects.
inline constexpr std::string_view NoEffect = "item '{}' has no entry in data/item_effects";
inline constexpr std::string_view EffectUnmapped =
    "item '{}' ({}): the extractor did not decode its case and no rule covers it";
inline constexpr std::string_view EvolutionUnmapped =
    "item '{}': no evolution target in data/item_effects or the ruleset";
inline constexpr std::string_view HealingUnmapped = "item '{}': no healing value in data/item_effects";
inline constexpr std::string_view HandlerRuleMissing = "item '{}' ({}): the ruleset has no rule for it";

// Shops.
inline constexpr std::string_view NotInStock = "item '{}' is not in stock";
inline constexpr std::string_view CannotAfford = "{} of item '{}' cost {}, the player has {}";
inline constexpr std::string_view TooMany = "at most {} of item '{}' can be bought now";
inline constexpr std::string_view NotSellable = "item '{}' cannot be sold";
inline constexpr std::string_view NotEnoughToSell = "the bag has {} of item '{}', not {}";

// Saves.
inline constexpr std::string_view SaveContext = "inventory state";
inline constexpr std::string_view SaveSlotCount = "the save has {} slots, not {}";
inline constexpr std::string_view SaveStack = "a stack must hold 1 to {} items";
inline constexpr std::string_view SaveMoney = "money must be from 0 to {}";

} // namespace dwe::game::inventory::messages
