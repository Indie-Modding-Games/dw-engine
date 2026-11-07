#pragma once

#include <string_view>

namespace dwe::session::field_items_messages {

inline constexpr std::string_view SectionText = "the field_items section";
inline constexpr std::string_view NotPositive = "{} must be more than zero";
inline constexpr std::string_view NotMapping = "{} must be a mapping";
inline constexpr std::string_view UnknownItem = "field items: no item has the original number {}";
inline constexpr std::string_view UnknownDrop =
    "field items: a beaten enemy dropped item '{}', which the item table lacks";
inline constexpr std::string_view RareSectionText = "the rare_items section";
inline constexpr std::string_view NotPercent = "{} must be from 0 to 100";
inline constexpr std::string_view UnknownRareItem =
    "rare items: the pool names item '{}', which the item table lacks";
inline constexpr std::string_view TriggerFailed = "field items: chest trigger {}: {}";

} // namespace dwe::session::field_items_messages
