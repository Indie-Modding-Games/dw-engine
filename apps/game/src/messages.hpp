#pragma once

#include <string_view>

namespace dwe::app::messages {

// The log messages of the game's start, as std::format templates for core::formatMessage.
inline constexpr std::string_view RulesetFailed = "game: ruleset {}: {}";
inline constexpr std::string_view RulesetActive = "game: ruleset {} {} ({}), sections {}";
inline constexpr std::string_view RulesetSection = "{} ({})";
inline constexpr std::string_view ListSeparator = ", ";

// Autotest: a wait for a map change's name that has not come yet (the map id).
inline constexpr std::string_view MapNameNotShown = "no map change shows the name of {}";

// --list-saves.
inline constexpr std::string_view CannotMountContent = "cannot mount the content: {}";
inline constexpr std::string_view CannotReadSaveSettings = "cannot read the save settings: {}";
inline constexpr std::string_view SavesFolder = "saves in {}: {} slots and {} autosave slots";
inline constexpr std::string_view SlotLine = "  {}{}";
inline constexpr std::string_view EmptySlot = "{}: empty";
inline constexpr std::string_view SavedSlot = "{}: \"{}\", {}:{:02} played, ruleset {}, saved {:%F %T} UTC by {}";
inline constexpr std::string_view HasBackup = " (has a backup)";

} // namespace dwe::app::messages
