#pragma once

#include <string_view>

// The log messages of the game app, as std::format templates for core::formatMessage. Templates take their arguments
// in the order of their placeholders.
namespace dwe::app::messages {

// The game's start: the ruleset in use.
inline constexpr std::string_view RulesetFailed = "game: ruleset {}: {}";
inline constexpr std::string_view RulesetActive = "game: ruleset {} {} ({}), sections {}";
inline constexpr std::string_view RulesetSection = "{} ({})";
inline constexpr std::string_view ListSeparator = ", ";

// The user's settings (user_settings.cpp).
inline constexpr std::string_view UserSettingsContext = "user settings";
inline constexpr std::string_view UserSettingsWhat = "the user settings";
inline constexpr std::string_view CannotReadUserSettings = "cannot read the user settings {}: {}; using the defaults";
inline constexpr std::string_view CannotWriteUserSettings = "cannot write the user settings {}: {}";
inline constexpr std::string_view CannotOpenFile = "cannot open the file";
inline constexpr std::string_view NoUserSettingsFolder = "the user settings are not saved: {}";

// The player's language (language.cpp).
inline constexpr std::string_view UnknownLanguage = "the packs have no texts in '{}'; they have {}";
inline constexpr std::string_view SavedLanguageGone = "the saved language '{}' is not in the packs any more";
inline constexpr std::string_view StartLanguage = "game: language {} ({})";
inline constexpr std::string_view LanguageChanged = "game: language changed to {}";
inline constexpr std::string_view CannotChangeLanguage = "game: cannot change the language: {}";
inline constexpr std::string_view FromSavedSetting = "saved setting";
inline constexpr std::string_view FromSystem = "system locale or default";

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
