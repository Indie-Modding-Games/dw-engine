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
inline constexpr std::string_view AudioStuckAtExit =
    "game: the sound device's open never returned; the game ends without waiting for it";
// The menus' sounds (ui/ui_sounds.yaml): a cue that cannot play stays silent.
inline constexpr std::string_view MenuSoundsSilent = "game: the menus are silent: {}";
inline constexpr std::string_view MenuSoundProblem = "game: a menu sound stays silent: {}";
inline constexpr std::string_view MenuSoundsNotMapping = "ui/ui_sounds is not a mapping of cues";
inline constexpr std::string_view MenuSoundUnknownCue = "ui/ui_sounds has no cue '{}'";
inline constexpr std::string_view MenuSoundMissing = "ui/ui_sounds gives no sound for '{}'";

// The UI's commands (game.cpp: apply): one no handler took, which the UI API lacks or the game
// cannot do now (docs/modding/ui-api.md).
inline constexpr std::string_view UiCommandUnhandled =
    "game: no one handled the UI command {} = {}";

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
    "game: the saved minimap mode '{}' is unknown; showing the minimap";
inline constexpr std::string_view UnknownSavedMinimapSize =
    "game: the saved minimap size '{}' is unknown; using the medium one";

// What walks the player (walk_control.cpp): a saved value that is not keys or mouse.
inline constexpr std::string_view UnknownSavedWalkControl =
    "game: the saved walk control '{}' is unknown; walking with the keys";

// The battle speed (battle_speed.cpp): a saved value that is not normal, key or fast.
inline constexpr std::string_view UnknownSavedBattleSpeed =
    "game: the saved battle speed '{}' is unknown; fighting at the original's speed";

// The field's shadows (graphics_options.cpp): a saved value that is not off, round or sun.
inline constexpr std::string_view UnknownSavedShadows =
    "game: the saved shadow mode '{}' is unknown; drawing the round shadows and the sun's";

// The field's sharpness options (graphics_options.cpp): a saved scenery_quality or texture_filter
// that is not one of their values.
inline constexpr std::string_view UnknownSavedGraphicsValue =
    "game: the saved {} '{}' is unknown; using the best quality";

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
inline constexpr std::string_view NoPlayerClip = "field scene: the player has no clip {}";
// The fishing (fishing_screen.cpp).
inline constexpr std::string_view SeaSerpentScene =
    "fishing: the sea serpent landed with no story to play its scene";

// The title screen (title_screen.cpp).
inline constexpr std::string_view TitleCommand = "game: command {}";
inline constexpr std::string_view SaveErased = "game: erased {}";
inline constexpr std::string_view CannotEraseSave = "game: erase: {}";

// The saves and the sleep in the game (game_saves.cpp, sleep_screen.cpp).
inline constexpr std::string_view GameSaveFailed = "game: save: {}";
inline constexpr std::string_view SleepFailed = "game: sleep: {}";

// --list-saves.
inline constexpr std::string_view CannotMountContent = "cannot mount the content: {}";
inline constexpr std::string_view CannotReadSaveSettings = "cannot read the save settings: {}";
inline constexpr std::string_view SavesFolder = "saves in {}: {} slots";
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
inline constexpr std::string_view PlaceFailed = "game: place: {}";
inline constexpr std::string_view WarpFailed = "game: warp: {}";
inline constexpr std::string_view WarpUsage =
    "'{}': expected '<map> [exit]' with a map of the packs";
inline constexpr std::string_view StorySetFailed = "game: story_set: {}";
inline constexpr std::string_view PartnerSetFailed = "game: partner_set: {}";
inline constexpr std::string_view LearnMoveFailed = "game: learn_move: {}";
inline constexpr std::string_view LearnMoveUsage = "\"{}\" is not \"<move> [place]\"";
inline constexpr std::string_view LearnMoveNotInSpecies = "{} has no move {}";
inline constexpr std::string_view NoStory = "the game has no story";
inline constexpr std::string_view NoNpcToPlace = "the map has no NPC with script id {}";
inline constexpr std::string_view BattleWinFailed = "game: battle_win: {}";
inline constexpr std::string_view BattleLoseFailed = "game: battle_lose: {}";
inline constexpr std::string_view NoFight = "no fight is running";
inline constexpr std::string_view BattleTimeUpFailed = "game: battle_time_up: {}";
inline constexpr std::string_view BattleStarted = "game: battle against {} on {}";
inline constexpr std::string_view BattleFinished = "game: battle over: {}";
inline constexpr std::string_view BattleDropLost = "game: no room on the ground for the dropped {}";
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
inline constexpr std::string_view WhenNotRun = "autotest: step {} skips its {} steps: {}";
inline constexpr std::string_view CareScolded = "game: the partner was scolded";
inline constexpr std::string_view UsedItem = "game: used {}: {}";
inline constexpr std::string_view UseItemFailed = "game: use_item: {}";
inline constexpr std::string_view DropItemFailed = "game: drop_item: {}";

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
inline constexpr std::string_view SaveWaitsForCare =
    "game: saving {} waits for the care scene to end";

// The partner's events and the debug evolve command (partner_events.cpp, game.cpp).
// The map's index, the reason (death_scene.cpp).
inline constexpr std::string_view WakeMapNotEntered = "game: the wake map {} cannot be entered: {}";
// The clip id, the model.
inline constexpr std::string_view PartnerClipMissing =
    "field: the partner's model {1} has no clip {0}; it plays its idle";
inline constexpr std::string_view FighterNotShown =
    "battle: fighter {} is not shown: the field draws no NPC with script id {}";
inline constexpr std::string_view FighterClipMissing =
    "field: the fighter's model {1} has no battle clip {0}; it keeps the clip it plays";
inline constexpr std::string_view ExitTimedOut =
    "battle: an NPC leaving after the battle is still on screen after {} s; the battle ends";
inline constexpr std::string_view PartnerModelNotShown =
    "game: the partner cannot be drawn as {}: {}";
inline constexpr std::string_view PartnerEventNotShown =
    "game: partner event {} (value {}) is not shown yet";
inline constexpr std::string_view EvolveFailed = "game: evolve: {}";
inline constexpr std::string_view SaveMachineFailed = "game: save_machine: {}";
inline constexpr std::string_view SaveMachineWaitsForCare =
    "game: the save machine opens once the care scene ends";
inline constexpr std::string_view GymFailed = "game: gym: {}";
inline constexpr std::string_view EvolveRefused = "game: evolve: the partner cannot become {} now";

// A problem of a warp of the map entered (assets::warpIssueMessage): the field disables the warp,
// or for a problem that leaves it working (assets::disablesWarp) only names it.
inline constexpr std::string_view WarpDisabled = "game: {}; the warp is disabled";
inline constexpr std::string_view WarpProblem = "game: {}";
// The asset of a door model, DOOR\DOORnn.TMD (dwx asset_names.hpp).
inline constexpr std::string_view DoorModelId = "model/door/door{:02}";
inline constexpr std::string_view DoorModelMissing =
    "[port:field-objects] loadDoors (map_object.c:355 loadStaticTMD): {}: {}; the object is not "
    "drawn";
inline constexpr std::string_view PropModelMissing =
    "[port:field-objects] the model {} of the field's props: {}; they are not drawn";
inline constexpr std::string_view SpriteCharactersMissing =
    "field: no {}: the sprite characters stay sprites";
inline constexpr std::string_view SpriteCharactersUnreadable =
    "field: {}: {}; the sprite characters stay sprites";
inline constexpr std::string_view SpriteCharacterModelMissing =
    "field: the {} of map {} stays a sprite: {}";
inline constexpr std::string_view SpriteCharacterClipMissing =
    "field: the {} of map {} has no animation {}; it stands with its idle";

// The options that change the rules (rule_options.hpp).
inline constexpr std::string_view DifficultyNoLevels =
    "the difficulty section needs `levels`, a mapping of each level's changes";
inline constexpr std::string_view DifficultyChangeShape =
    "a difficulty level is a list of changes, each with `section`, `path` and either `value` or "
    "`percent`";
inline constexpr std::string_view DifficultyPercent =
    "a difficulty change's `percent` is a whole number from 0";
inline constexpr std::string_view RuleOptionsUnread = "options: the rules' options: {}";
inline constexpr std::string_view RuleOptionNoValue =
    "options: the ruleset has no value {}.{}; the option leaves it";
inline constexpr std::string_view RuleOptionNotNumber =
    "options: {}.{} is not a whole number, so no percent of it is set";
inline constexpr std::string_view RuleOptionRefused = "options: {}.{} cannot take {}";
inline constexpr std::string_view RuleOptionSet = "options: {}.{} = {}";

} // namespace dwe::app::messages
