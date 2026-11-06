#pragma once

#include <string_view>

namespace dwe::session::fishing_messages {

inline constexpr std::string_view NoFishing =
    "fishing: no fishing table or no fishing rules; no map has fishing";
inline constexpr std::string_view UnknownItem = "fishing: data/fishing names item '{}', which the "
                                                "item table lacks";
inline constexpr std::string_view UnknownItemNumber =
    "fishing: data/fishing names item number {}, which the item table lacks";
inline constexpr std::string_view NoKinds = "fishing: data/fishing has no fish kinds";
inline constexpr std::string_view NoShoreline =
    "fishing: map '{}' names no shoreline of data/fishing; it has no fishing";
inline constexpr std::string_view BaitNotHeld = "fishing: the eaten bait {} is not in the bag";
inline constexpr std::string_view RodTriggerFailed = "fishing: rod trigger {}: {}";

} // namespace dwe::session::fishing_messages
