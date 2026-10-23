#pragma once

#include <string_view>

namespace dwe::session::poop_messages {

inline constexpr std::string_view SectionText = "the poop_piles section";
inline constexpr std::string_view SaveName = "poop_piles";
inline constexpr std::string_view SaveText = "the poop_piles save section";
inline constexpr std::string_view PileText = "a poop pile";
inline constexpr std::string_view NotPositive = "{} must be more than zero";
inline constexpr std::string_view BadModel = "model '{}': {}";
inline constexpr std::string_view MissingInSave =
    "poop piles: the save has no poop_piles section; no piles on the ground";
inline constexpr std::string_view NoPiles =
    "poop piles: the care rules cannot be read ({}); the partner's piles are not kept";

} // namespace dwe::session::poop_messages
