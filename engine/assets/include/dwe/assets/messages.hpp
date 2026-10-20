#pragma once

#include <string_view>

namespace dwe::assets::messages {

// What the readers of the base pack's game tables call each part in their errors ("a raise row: ...").
inline constexpr std::string_view DigimonTableText = "the digimon table";
inline constexpr std::string_view DigimonRowText = "a digimon row";
inline constexpr std::string_view RaiseTableText = "the raise table";
inline constexpr std::string_view RaiseRowText = "a raise row";
inline constexpr std::string_view ViewText = "a view offset";
inline constexpr std::string_view SpecialsText = "special";
inline constexpr std::string_view MovesText = "moves";
inline constexpr std::string_view HungerTimesText = "hunger_times";

} // namespace dwe::assets::messages
