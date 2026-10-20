#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::assets::messages {

using core::formatMessage;

// What the readers of the base pack's game tables call each part in their errors ("a raise row: ...").
inline constexpr std::string_view DigimonTableText = "the digimon table";
inline constexpr std::string_view DigimonRowText = "a digimon row";
inline constexpr std::string_view RaiseTableText = "the raise table";
inline constexpr std::string_view RaiseRowText = "a raise row";
inline constexpr std::string_view ViewText = "a view offset";
inline constexpr std::string_view SpecialsText = "special";
inline constexpr std::string_view MovesText = "moves";
inline constexpr std::string_view HungerTimesText = "hunger_times";
inline constexpr std::string_view MoveTableText = "move table";
inline constexpr std::string_view EvolutionTableText = "evolution table";
inline constexpr std::string_view MoveText = "a move";
inline constexpr std::string_view LearnChancesText = "learn_chances";
inline constexpr std::string_view RequirementText = "an evolution requirement";
inline constexpr std::string_view StatsGainText = "an evolution stats gain";
inline constexpr std::string_view PathText = "an evolution path";
inline constexpr std::string_view ItemTargetText = "an evolution item target";

// The errors of the move and evolution readers, as std::format templates for core::formatMessage.
inline constexpr std::string_view TableParse = "{}: {}";
inline constexpr std::string_view RepeatedEntry = "{} '{}' is listed twice";
inline constexpr std::string_view RepeatedIndex = "{} '{}' repeats the index {}";

} // namespace dwe::assets::messages
