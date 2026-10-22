#pragma once

#include <string_view>

// The error messages of game/save. Templates take their arguments in the order of their
// placeholders.
namespace dwe::game::save::messages {

// Migrations.
inline constexpr std::string_view VersionTooNew =
    "{} is version {}, newer than this game reads ({}): it was saved by a newer version of the game";
inline constexpr std::string_view VersionTooOld =
    "{} is version {}, older than this game reads ({})";
inline constexpr std::string_view MigrationFailed = "{}: migrating version {} to {}: {}";
inline constexpr std::string_view FormatWhat = "the save format";
inline constexpr std::string_view SectionWhat = "section '{}'";

// The save file.
inline constexpr std::string_view SaveFileContext = "save file";
inline constexpr std::string_view InSaveFile = "save file: {}";
inline constexpr std::string_view NotASave = "save file: not a save (it needs `format: {}`)";
inline constexpr std::string_view SaveWhat = "the save";
inline constexpr std::string_view NotComplete = "the file is not complete";
inline constexpr std::string_view BadTime = "a time is written YYYY-MM-DDTHH:MM:SSZ, in UTC";
inline constexpr std::string_view NegativePlayTime = "the play time cannot be negative";
inline constexpr std::string_view SectionsNotMapping =
    "sections must be a mapping of section names";
inline constexpr std::string_view BadSectionName = "'{}' is not a section name (a-z, 0-9 and _)";
inline constexpr std::string_view BadSectionVersion = "a section version is 1 or more";
inline constexpr std::string_view AutosaveReasonsWhat = "autosave_reasons";
inline constexpr std::string_view SummaryWhat = "the summary";
inline constexpr std::string_view BadAutosaveReason =
    "'{}' is not an autosave reason name (a-z, 0-9 and _)";

// Sections.
inline constexpr std::string_view SectionRegisteredTwice = "section '{}' is registered twice";
inline constexpr std::string_view InSection = "section '{}': {}";
inline constexpr std::string_view SectionMissing = "the save has no data for it";

// Storage.
inline constexpr std::string_view BadFileName =
    "'{}' is not a save file name (a-z, 0-9, _ and ., not starting with .)";
inline constexpr std::string_view CannotOpen = "cannot open '{}': {}";
inline constexpr std::string_view CannotCreateFolder = "cannot create '{}': {}";
inline constexpr std::string_view CannotWrite = "cannot write '{}'";
inline constexpr std::string_view CannotBackUp = "cannot back up '{}': {}";
inline constexpr std::string_view CannotReplace = "cannot replace '{}': {}";
inline constexpr std::string_view StorageFull = "cannot write '{}': the storage is full";

// Settings and the ruleset's save section.
inline constexpr std::string_view SettingsContext = "save settings";
inline constexpr std::string_view InSettings = "save settings: {}";
inline constexpr std::string_view SettingsWhat = "the save settings";
inline constexpr std::string_view AutosaveWhat = "autosave";
inline constexpr std::string_view SaveSectionWhat = "the save section";
inline constexpr std::string_view ManualSlotsRange = "manual_slots must be 1 to {}";
inline constexpr std::string_view AutosaveSlotsRange = "autosave_slots must be 0 to {}";
inline constexpr std::string_view CoalesceTooShort =
    "coalesce_seconds must be {} or more: autosave slots rotate by their time in whole seconds";

// Slots.
inline constexpr std::string_view InFile = "{}: {}";
inline constexpr std::string_view NoSuchSlot = "there is no {} slot {}";
inline constexpr std::string_view ManualSlotKind = "save";
inline constexpr std::string_view AutosaveSlotKind = "autosave";
inline constexpr std::string_view SlotEmpty = "{} is empty";
inline constexpr std::string_view AutosaveOff = "the autosave is off: there are no autosave slots";

} // namespace dwe::game::save::messages
