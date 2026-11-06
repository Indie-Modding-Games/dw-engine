#pragma once

#include <string_view>

namespace dwe::session::cards_messages {

inline constexpr std::string_view SaveName = "cards";
inline constexpr std::string_view CopiesKey = "copies";
inline constexpr std::string_view SaveText = "the cards save section";
inline constexpr std::string_view MissingInSave =
    "cards: the save has no cards section; no cards held";
inline constexpr std::string_view NoCards =
    "cards: no card table or no cards rules; the player has no cards";
inline constexpr std::string_view DigimonNotSet = "cards: variable {} for card {}: {}";
inline constexpr std::string_view NoSuchCard = "cards: no card has the number {}";

} // namespace dwe::session::cards_messages
