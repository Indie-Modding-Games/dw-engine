#pragma once

#include <string_view>

// The log messages of the game app, as std::format templates for core::formatMessage. Templates
// take their arguments in the order of their placeholders.
namespace dwe::app::messages {

// The game's start: the ruleset in use.
inline constexpr std::string_view RulesetFailed = "game: ruleset {}: {}";
inline constexpr std::string_view RulesetActive = "game: ruleset {} {} ({}), sections {}";
inline constexpr std::string_view RulesetSection = "{} ({})";
inline constexpr std::string_view ListSeparator = ", ";

// The sound device (game_audio.cpp).
inline constexpr std::string_view AudioSilent = "game: no sound: {}; the game goes on silent";

// The dev assets banner (dev_assets_banner.cpp), logged in the player's language.
inline constexpr std::string_view DevAssetsBanner = "game: {}";

// The user's settings (user_settings.cpp).
inline constexpr std::string_view UserSettingsContext = "user settings";
inline constexpr std::string_view UserSettingsWhat = "the user settings";
inline constexpr std::string_view CannotReadUserSettings =
    "cannot read the user settings {}: {}; using the defaults";
inline constexpr std::string_view CannotWriteUserSettings = "cannot write the user settings {}: {}";
inline constexpr std::string_view CannotOpenFile = "cannot open the file";
inline constexpr std::string_view NoUserSettingsFolder = "the user settings are not saved: {}";

// The player's language (language.cpp).
inline constexpr std::string_view UnknownLanguage = "the packs have no texts in '{}'; they have {}";
inline constexpr std::string_view SavedLanguageGone =
    "the saved language '{}' is not in the packs any more";
inline constexpr std::string_view StartLanguage = "game: language {} ({})";
inline constexpr std::string_view LanguageChanged = "game: language changed to {}";
inline constexpr std::string_view CannotChangeLanguage = "game: cannot change the language: {}";
inline constexpr std::string_view FromSavedSetting = "saved setting";
inline constexpr std::string_view FromSystem = "system locale or default";
// The player's text font (text_font_setting.cpp): a name that is neither modern nor original.
inline constexpr std::string_view UnknownTextFont =
    "game: unknown text font '{}'; use modern or original";
inline constexpr std::string_view UnknownSavedTextFont =
    "game: the saved text font '{}' is unknown; using the default";
// The player's UI skin (ui_skin_setting.cpp): the id and why it cannot be put on.
inline constexpr std::string_view UnusableUiSkin = "game: cannot use the UI skin '{}': {}";

// The player's minimap mode (minimap_setting.cpp): a saved value that is not auto, on or off.
inline constexpr std::string_view UnknownSavedMinimap =
    "game: the saved minimap mode '{}' is unknown; using auto";

// Free exploration (free_exploration.cpp) on from the start, by --explore or the saved setting.
inline constexpr std::string_view FreeExplorationAtStart =
    "game: free exploration on: the scripts' map changes go anywhere they can, whatever the story";

// Autotest: a wait for a map change's name that has not come yet (the map id).
inline constexpr std::string_view MapNameNotShown = "no map change shows the name of {}";

// The command line (options.cpp).
inline constexpr std::string_view NewGameOrLoad = "--new-game and --load exclude each other";

// The game session (session_start.cpp, partner_tab.cpp).
inline constexpr std::string_view SessionFailed = "game: session: {}";
inline constexpr std::string_view SessionStarted = "game: new game, seed {}";
inline constexpr std::string_view SessionLoaded = "game: loaded {}";
inline constexpr std::string_view SessionNoSaves = "game: the game is not saved: {}";
inline constexpr std::string_view NoGameClock = "field scene: no game clock";

// The title screen (title_screen.cpp).
inline constexpr std::string_view TitleCommand = "game: command {}";

// --list-saves.
inline constexpr std::string_view CannotMountContent = "cannot mount the content: {}";
inline constexpr std::string_view CannotReadSaveSettings = "cannot read the save settings: {}";
inline constexpr std::string_view SavesFolder = "saves in {}: {} slots and {} autosave slots";
inline constexpr std::string_view SlotLine = "  {}{}";
inline constexpr std::string_view EmptySlot = "{}: empty";
inline constexpr std::string_view SavedSlot =
    "{}: \"{}\", {}:{:02} played, ruleset {}, saved {:%F %T} UTC by {}";
inline constexpr std::string_view HasBackup = " (has a backup)";

// The battle trigger and screen (battle_trigger.cpp, battle_screen.cpp).
inline constexpr std::string_view NoMapNpc = "the map {} has no NPC {}";
inline constexpr std::string_view NpcNotAFighter =
    "NPC {} of the map {} is no Digimon of the table";
inline constexpr std::string_view NoMapFighter = "the map {} has no NPC to fight";
inline constexpr std::string_view BadNpcList = "'{}' is not a list of NPC numbers";
inline constexpr std::string_view BattleAlreadyRunning = "a battle is already under way";
inline constexpr std::string_view BattleStartFailed = "game: battle: {}";
inline constexpr std::string_view BattleStarted = "game: battle against {} on {}";
inline constexpr std::string_view BattleFinished = "game: battle over: {}";
inline constexpr std::string_view NoSpeciesTable = "game: the ruleset names no Digimon table";
inline constexpr std::string_view BattleCommandRefused = "game: battle: command {} refused: {}";
inline constexpr std::string_view BattleBagRefused = "game: battle: the bag did not open: {}";
inline constexpr std::string_view BattleThrowRefused =
    "game: battle: no throw from bag slot {}: {}";
inline constexpr std::string_view BattleItemThrown = "game: battle: threw item '{}'";
inline constexpr std::string_view EmptyBagSlot = "the slot is empty";
inline constexpr std::string_view ItemNotThrowable = "the item cannot be thrown in battle";

// The partner's menu (partner_menu.cpp).
inline constexpr std::string_view BagCommandFailed = "game: bag: {} failed: {}";
inline constexpr std::string_view BadBagMove = "game: bag: '{}' is not two slots";
inline constexpr std::string_view BadGiveItem = "'{}' is not an item id and a count";
inline constexpr std::string_view UnknownItem = "no item '{}'";
inline constexpr std::string_view GiveItemFailed = "game: give_item: {}";
inline constexpr std::string_view GaveItem = "game: gave {} x{} ({} added)";
inline constexpr std::string_view ScriptIfNotRun = "autotest: step {} not run: {}";
inline constexpr std::string_view CareScolded = "game: the partner was scolded";
inline constexpr std::string_view UsedItem = "game: used {}: {}";
inline constexpr std::string_view UseItemFailed = "game: use_item: {}";

// The care debug commands (care_commands.cpp).
inline constexpr std::string_view CareCommandFailed = "game: care command: {}";
inline constexpr std::string_view NoGame = "no game is being played";
inline constexpr std::string_view CannotSleep = "the partner is not sleepy";
inline constexpr std::string_view BadTrainArgument = "'{}' is not a training place and a course";
inline constexpr std::string_view BadClockSpeed = "'{}' is not a clock speed";
inline constexpr std::string_view CareToilet = "game: the partner used the toilet";
inline constexpr std::string_view CareSlept = "game: the partner slept";
inline constexpr std::string_view CareTrained = "game: the partner trained {} {}";
inline constexpr std::string_view ClockSpeedSet = "game: clock speed {}";

// The save debug command (save_command.cpp).
inline constexpr std::string_view NotAManualSlot = "'{}' is not a save slot such as slot_01";
inline constexpr std::string_view SaveCommandFailed = "game: save: {}";
inline constexpr std::string_view SavedToSlot = "game: saved to {}";

// The partner's events and the debug evolve command (partner_events.cpp, game.cpp).
inline constexpr std::string_view PartnerModelNotShown =
    "game: the partner cannot be drawn as {}: {}";
inline constexpr std::string_view PartnerEventNotShown =
    "game: partner event {} (value {}) is not shown yet";
inline constexpr std::string_view EvolveFailed = "game: evolve: {}";
inline constexpr std::string_view EvolveRefused = "game: evolve: the partner cannot become {} now";

// A problem of a warp of the map entered (assets::warpIssueMessage): the field disables the warp,
// or for a problem that leaves it working (assets::disablesWarp) only names it.
inline constexpr std::string_view WarpDisabled = "game: {}; the warp is disabled";
inline constexpr std::string_view WarpProblem = "game: {}";

} // namespace dwe::app::messages
