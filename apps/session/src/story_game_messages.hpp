#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

// The error and log messages of the story's game orders, as std::format templates for
// core::formatMessage.
namespace dwe::session::story_game_messages {

using core::formatMessage;

inline constexpr std::string_view StoryValuesText = "the story_values section";
inline constexpr std::string_view LimitNegative = "{} cannot be negative";
inline constexpr std::string_view FloorPositive = "happiness_floor ({}) cannot be above 0";
inline constexpr std::string_view OverflowOutside = "date_overflow {} is outside the calendar";
inline constexpr std::string_view DrawRangeTooSmall =
    "daily_change draw_range ({}) must be 2 or more";
inline constexpr std::string_view VariableNotWritten =
    "the daily change could not write variable {}: {}";
inline constexpr std::string_view NoStoryVm = "the story's script VM is not built";
inline constexpr std::string_view NoSuchVariable = "no story variable has the number {}";
inline constexpr std::string_view NoSuchTrigger = "no story trigger has the number {}";
inline constexpr std::string_view NoSuchItem = "no item has the number {}";
inline constexpr std::string_view NoSuchStat = "no stat has the number {} (0 to {})";
inline constexpr std::string_view NoSuchMove = "no move has the number {}";
inline constexpr std::string_view NoSuchSpecies = "no species has the number {}";
inline constexpr std::string_view NoBag = "the story orders have no bag";
inline constexpr std::string_view NoMoves = "the story orders have no moves";
inline constexpr std::string_view SlotCountRefused = "set_inventory_size {}: {}";
inline constexpr std::string_view PartnerUnnumbered = "the partner '{}' has no number";

} // namespace dwe::session::story_game_messages
