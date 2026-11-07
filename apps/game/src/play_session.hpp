#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/debug/debug_ui.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/render/upscale_cache.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app_flow.hpp"
#include "autotest.hpp"
#include "award_screen.hpp"
#include "battle_ground.hpp"
#include "battle_screen.hpp"
#include "battle_speed.hpp"
#include "boss_arrival_scene.hpp"
#include "bug_report.hpp"
#include "care_commands.hpp"
#include "chart_requirements.hpp"
#include "curling_screen.hpp"
#include "death_scene.hpp"
#include "egg_box_screen.hpp"
#include "evolution_scene.hpp"
#include "feed_scene.hpp"
#include "field_hud.hpp"
#include "field_items_screen.hpp"
#include "field_numbers.hpp"
#include "field_poop.hpp"
#include "field_scene.hpp"
#include "field_story_props.hpp"
#include "field_toilet.hpp"
#include "fishing_screen.hpp"
#include "frame_stats.hpp"
#include "free_exploration.hpp"
#include "gym_screen.hpp"
#include "item_keeper_screen.hpp"
#include "jukebox_screen.hpp"
#include "minimap_enemies.hpp"
#include "minimap_items.hpp"
#include "minimap_setting.hpp"
#include "movie_rules.hpp"
#include "naming_box.hpp"
#include "options.hpp"
#include "partner_menu.hpp"
#include "praise_scold_call.hpp"
#include "recruit_bar.hpp"
#include "remote_control.hpp"
#include "save_command.hpp"
#include "save_indicator.hpp"
#include "save_machine.hpp"
#include "session_start.hpp"
#include "shop_job_bar.hpp"
#include "shop_screen.hpp"
#include "sleep_screen.hpp"
#include "state_trace.hpp"
#include "story_field_link.hpp"
#include "story_scene_glue.hpp"
#include "story_state_json.hpp"
#include "story_textbox.hpp"
#include "tournament_bracket_screen.hpp"
#include "tournament_schedule_screen.hpp"
#include "user_settings.hpp"
#include "weather_ambience.hpp"
#include "world_map_screen.hpp"

namespace dwe::app {

// Smooth motion: a UI setting and the pause menu's command with the same name (true or false), on
// by default (game::field::Field::setSmoothMotion).
inline constexpr std::string_view SmoothMotionSetting = "smooth_motion";
// The time in digits: a UI setting and the pause menu's command with the same name (true or false),
// on by default. The field HUD (ui/hud) shows the game time as HH:MM under its dial clock with it.
inline constexpr std::string_view DigitalTimeSetting = "digital_time";
// The pause menu's "Translated signs" (FieldScene::setTranslatedSigns).
inline constexpr std::string_view TranslatedSignsSetting = "translated_signs";
// The status page's extras: a UI setting and the pause menu's command with the same name (true or
// false), on by default. The partner's menu (ui/partner_menu) shows the tiredness bar and the care
// mistakes with it.
inline constexpr std::string_view StatusExtrasSetting = "status_extras";
// The canonical stages: a UI setting and the pause menu's command with the same name (true or
// false), off by default. The chart (ui/pause_menu) names the stages and draws the Mega box with
// it.
inline constexpr std::string_view CanonicalStagesSetting = "canonical_stages";
// The rare items: a UI setting and the pause menu's command with the same name (true or false), off
// by default. The field items lay the ruleset's rare items with it (FieldItemsAdapter).
inline constexpr std::string_view RareItemsSetting = "rare_items";
// Long names: a UI setting and the pause menu's command with the same name (true or false), on by
// default. The naming box takes names of up to LongNameLength letters with it (naming_box.hpp).
inline constexpr std::string_view LongNamesSetting = "long_names";
// Continuous music: a UI setting and the pause menu's command with the same name (true or false),
// on by default. The session's music lets a map's song go on when it is the one playing
// (session::SessionMusic::setContinuousMusic).
inline constexpr std::string_view ContinuousMusicSetting = "continuous_music";
// The need bubble with several needs at once (an engine improvement, on unless turned off).
inline constexpr std::string_view NeedBubbleStackSetting = "need_bubble_stack";
// Enemy life bars over every enemy still standing, cups too (an engine improvement, off unless
// turned on; BattleScreen::setEnemyHpBars).
inline constexpr std::string_view EnemyHpBarsSetting = "enemy_hp_bars";
// The weather (an engine improvement, on unless turned off): a UI setting and the options'
// command with the same name (true or false), on the title screen and in the game.
inline constexpr std::string_view WeatherSetting = "weather";
// The seasons (an engine improvement, on unless turned off): a UI setting and the options' command
// with the same name (true or false), on the title screen and in the game.
inline constexpr std::string_view SeasonsSetting = "seasons";
// The calendar's season for the menus (spring, summer, autumn, winter; empty with the option off).
inline constexpr std::string_view SeasonNameSetting = "season";

// What every game uses of the app; all of it outlives the games.
struct PlayContext {
  const Options* options = nullptr;
  // False for an autotest, which writes no save unless --saves names a folder.
  bool keepsSaves = true;
  const assets::VirtualFileSystem* files = nullptr;
  rhi::Device* device = nullptr;
  render::Renderer* renderer = nullptr;
  ui::Ui* ui = nullptr;
  // Where the game's sounds play; nullptr plays none.
  audio::Audio* sound = nullptr;
  debug::DebugUi* debugUi = nullptr;
  UserSettings* userSettings = nullptr;
  // Where the player's settings are saved; nothing for runs that must not touch them (autotests).
  std::optional<std::filesystem::path> settingsFile;
  // Where HD scenery's upscaler keeps what it made; nothing keeps nothing.
  std::shared_ptr<const render::UpscaleCache> upscaleCache = nullptr;
};

// The console's and the scripts' commands on the game being played (give_item, use_item, drop_item,
// battle, evolve, the care commands and save). The script VM outlives the games, so they are bound
// once and queued here; the game being played applies them between frames.
struct SessionCommands {
  std::optional<std::string> gift;
  std::optional<std::string> use;
  std::optional<std::string> groundDrop;
  std::optional<std::string> battle;
  bool battleWin = false;
  bool battleLose = false;
  std::optional<std::string> battleTimeUp;
  std::optional<std::string> evolution;
  CareCommands care;
  SaveCommand save;
  bool saveMachine = false;
  std::optional<std::string> gym;
  // model_characters("on"|"off"): the 3D characters option for this game, not saved.
  std::optional<bool> modelCharacters;
  // place("<who> <x> <z> <rotation>") commands, in order (place_command.hpp).
  std::vector<std::string> placements;
  // story_set("trigger <n> on|off" | "var <n> <value>") commands, in order (story_set_command.hpp).
  std::vector<std::string> storyWrites;
  // partner_set("<value> <amount>") commands, in order (partner_set_command.hpp).
  std::vector<std::string> partnerWrites;
  // learn_move("<move>") commands, in order (partner_events.hpp).
  std::vector<std::string> lessons;

  void bind(script::ScriptVm& scripts);
  // Forgets what was queued with no game to apply it to, and says so; a route that asked for a save
  // fails.
  void drop(Autotest* autotest);
};

// What lives as long as one game (docs/architecture/guardar-y-cargar.md, 3.1): the session, the
// field, the battle, the partner's menu and the HUD. It is built whole from a new game or a save
// and torn down whole.
class PlaySession {
  // Only create builds a game, which needs make_unique to reach the constructor.
  struct Key {
    explicit Key() = default;
  };

public:
  static core::Result<std::unique_ptr<PlaySession>> create(const PlayContext& context,
                                                           const PlayStart& start);

  PlaySession(const PlaySession&) = delete;
  PlaySession& operator=(const PlaySession&) = delete;
  PlaySession(PlaySession&&) = delete;
  PlaySession& operator=(PlaySession&&) = delete;
  ~PlaySession();
  PlaySession(Key key, const PlayContext& context, GameStart game,
              std::unique_ptr<FieldScene> scene);

  [[nodiscard]] FieldScene& scene() { return *scene_; }
  [[nodiscard]] const FieldScene& scene() const { return *scene_; }

  // One logic tick: the battle, or the field and the game time unless a menu pauses them. A tick in
  // which the field does not take the player's walk (a menu, a screen or scene that runs instead,
  // a gym course) drops the click-to-move target, so the player never walks on to it afterwards.
  void tick(const game::field::FieldInput& intent, const FishingButtons& fishing);
  // Counts a tick of play time (GameSession::countPlayTime, clock.c: tickPlaytime): the app calls
  // it on every tick of a played game but the paused ones and those of a movie, menus, battles and
  // scenes included.
  void countPlayTime(double seconds) { game_.session->countPlayTime(seconds); }
  // Whether a click on the ground is a walk order now: the last tick gave the field the player's
  // walk and nothing held him (no script or textbox, care scene, map change or screen).
  [[nodiscard]] bool walksByPointer() const;

  // Takes the UI's commands for the game (the battle, free exploration, the partner's menu); false
  // for any other.
  bool handle(const ui::UiCommand& command);
  // The player's page of the pause menu, published just before the menu shows (Tab, Esc, the
  // HUD's button), so it draws it once as it opens (#221).
  void preparePauseMenu() { partnerMenu_.publishPlayer(true); }

  // Once per frame after the commands: runs the queued session commands, follows the map changes
  // and publishes the HUD. An autotest hears of map changes and failed saves.
  void update(SessionCommands& commands, Autotest* autotest, FrameStats& stats, double frameSeconds,
              float alpha);

  // How much the map change's or the sleep's fade takes away from the screen, from 0 to 1.
  [[nodiscard]] float fadeLevel() const;
  // How much white the story's fade to white adds to the screen, from 0 to 1.
  [[nodiscard]] float whiteLevel() const { return storyProps_.whiteness(); }
  // Feeds the story's textboxes the player's keys, once per tick while no menu takes them.
  void pressStory(const input::InputState& input);
  // Feeds the battle the buttons it reads outside its document: `extra`, the original's square,
  // calls the finisher.
  void pressBattle(const input::InputState& input);
  // Batch training (gym_batch.hpp): the sessions a course the story starts runs in a row.
  void setGymSessions(int sessions) { gymScreen_.setSessions(sessions); }
  // What the fight on screen asks of a quick battle's speed (battle_speed.hpp).
  [[nodiscard]] BattleSpeedMoment battleSpeedMoment() const {
    return {.fighting = battleScreen_.fighting(), .reacting = battleScreen_.reacting()};
  }
  // The item keeper's, the discard menu's and the trade menu's buttons the menus do not navigate
  // with (move by tens, move all, the jump to the ends, the description), and the confirm button
  // held for a chest's notice.
  void pressItemKeeper(const input::InputState& input);
  // The partner menu's buttons the menus do not navigate with: `extra` opens the tech help.
  void pressPartnerMenu(const input::InputState& input);
  // The stones game's arrows and confirm button, while it runs.
  void pressCurling(const input::InputState& input) { curlingScreen_.press(input); }
  // The island map's key (world_map.hpp): opens the map when `canOpen` (the player walks freely and
  // no window, pause or fade holds the game) and the option allows it, or closes it.
  void pressWorldMap(const input::InputState& input, bool canOpen);
  [[nodiscard]] bool worldMapOpen() const { return worldMapScreen_.isOpen(); }
  // What the keyboard typed, for the story's naming box while it is open (takesText).
  [[nodiscard]] bool takesText() const;
  // Whether the menu button belongs to the game rather than opening the menu: a naming box takes
  // it, and a gym course takes it as the original's cancel.
  [[nodiscard]] bool holdsMenu() const;
  // Why the game menu stays shut now (player_menu_gate.hpp: a fade, a script, the dialogue, a
  // battle or scene, the player held), or empty when Tab or the HUD's button opens it.
  [[nodiscard]] std::string_view menuShutBy() const;
  // Whether a fade runs (fade.c: FADE_PROTECTION, or any other fade darkening the screen): the
  // pause and the pad's Select options wait for its end (utils2.c: handlePause).
  [[nodiscard]] bool fadeProtected() const;
  void type(std::string_view text, bool enter);

  // Where the characters and the battle's fighters stand after this tick, and the buttons held in
  // it, for --trace (state_trace.hpp).
  [[nodiscard]] TraceFrame traceFrame(std::uint64_t tick, const input::InputState& input) const;

  // The control channel's reload_map (FieldScene::reloadMap): the current map read again from the
  // packs and entered again, by `exit` or where the player stands, and the story's modules and
  // patches read again on its next section (the story's state kept). Refused during a battle or
  // while a story section runs (mapEditable).
  core::Result<void> reloadMap(std::optional<std::int32_t> exit);
  // Why the map editor's commands may not change the map now: a battle runs, or a story script or
  // section runs; nothing when they may.
  [[nodiscard]] core::Result<void> mapEditable() const;
  // Whether a story script or section runs or is about to start.
  [[nodiscard]] bool storyBusy() const;
  // The control channel's preview_map (FieldScene::previewMap): `text` over the map's data and
  // patches, with the rights of `pack` when it names a mounted pack (no base rights otherwise).
  // Refused as mapEditable says. True when the map was entered again.
  core::Result<bool> previewMap(const std::string& mapId, std::string text,
                                const std::optional<std::string>& pack);
  // The control channel's preview_clear (FieldScene::clearPreview); false without a preview.
  core::Result<bool> clearPreview();
  // The control channel's story_set: a trigger or a variable written; refused while a story script
  // runs.
  core::Result<void> storySet(const StoryWrite& write);
  // The control channel's story_run: the section asked for at the next frame of the story, with
  // the code sent in place of the packs' (StoryLuaHost::stageSection); refused while a story
  // script runs.
  core::Result<void> storyRun(const StoryRun& run);
  // Where the characters stand and the view, for the control channel's live, in the mode "field"
  // or "battle"; a movie or the opening's questions over it, and the log, are the caller's.
  [[nodiscard]] ControlLive live() const;
  // The story's triggers, variables and running script, and the camera, for story_state.
  [[nodiscard]] StoryStateView storyStateView() const;

  // Whether the save indicator is on screen, so the app draws its figure this frame.
  [[nodiscard]] bool saveIndicatorShown() const {
    return saveIndicator_ && saveIndicator_->shown();
  }

  // Saves the game to a slot (AppFlow, for a new game's slot).
  [[nodiscard]] core::Result<void> saveTo(game::save::SlotId slot) const {
    return game_.session->save(slot);
  }

  // Whether the player chose to go back to the title screen, once (docs/architecture/
  // guardar-y-cargar.md, 3.8).
  [[nodiscard]] bool takeToTitle() { return std::exchange(toTitle_, false); }

  // Whether the player chose to save and quit the game from the pause menu, once: the app closes
  // the window when it takes it.
  [[nodiscard]] bool takeQuitGame() { return std::exchange(quitGame_, false); }

  // The partner's status as the status page shows it (partner.* settings).
  [[nodiscard]] screens::ScreenSettings partnerStatus() const;
  // The story's triggers and variables, when the game runs the story (autotest's story.* settings).
  [[nodiscard]] const game::story::StoryState* storyState() const;
  [[nodiscard]] std::string_view partnerSpecies() const {
    return game_.session->partner().state().species;
  }
  [[nodiscard]] const game::partner::Conditions& partnerConditions() const {
    return game_.session->partner().state().conditions;
  }
  // The game's raise table with its ruleset's species patches: where the status portrait places a
  // form the patches add (the hidden content), which the disc's table lacks.
  [[nodiscard]] core::Result<assets::RaiseTable>
  raiseTable(const assets::VirtualFileSystem& files) const;
  // A species' size in the field (its patch's field_scale, else 1), which the portrait keeps too.
  [[nodiscard]] float speciesScale(std::string_view species) const;

  // What a bug report says of the game now (bug_report.hpp; the app fills in the build and the
  // date).
  [[nodiscard]] BugReportState reportState() const;
  // The save a bug report holds, as a save file's text: the game as it is when it could be saved
  // now, else the last snapshot of a free field (`save` says which, and why); nothing when there is
  // neither.
  [[nodiscard]] std::optional<std::string> reportSave(ReportSave& save) const;

  // The movie the game asks the app for (main.c: main): ED2 once a game saved after the ending is
  // loaded (MAIN_STATE 2, asked as the session is created), EDR once routine 53 ends the game
  // (MAIN_STATE 3). Taking the request clears it.
  void requestMovie(MovieMoment moment) { movieRequest_ = moment; }
  [[nodiscard]] std::optional<MovieMoment> takeMovieRequest() {
    return std::exchange(movieRequest_, std::nullopt);
  }
  // A movie over this game holds its music, and lets it go once over (main.c: main, finalizeMusic
  // and initializeMusic around playMovie; session::SessionMusic::holdForMovie).
  void holdMusicForMovie(bool hold);

private:
  void tickSystems(const game::field::FieldInput& intent, const FishingButtons& fishing);
  void tickWater();
  [[nodiscard]] bool paused() const;
  // How the field animates: running, resting behind the game menu or frozen (field_clock.hpp).
  [[nodiscard]] FieldClock fieldClock() const;
  // A screen, a scene or a battle that stops the field, the menus aside.
  [[nodiscard]] bool screenStops() const;
  // What keeps the game from being saved as it is now (a battle, a script, a scene), or empty when
  // the field is free.
  [[nodiscard]] std::string_view busyWith() const;
  // Keeps a snapshot of the game while the field is free, every few seconds, for a bug report
  // made while it is not.
  void keepFieldSnapshot(double frameSeconds);
  // The sulking partner's butterfly (butterfly.c), shown or taken away.
  void showButterfly(bool partnerNormal, const session::GameSession& session);
  // The shop job's progress bar (shop_job_bar.hpp), published when what it shows changed.
  void publishShopJob();
  // Publishes hud.minimap_allowed when it changes.
  // Draws the minimap of the map's collision again (after a preview swapped it in place).
  void refreshMinimap();
  void publishMinimapAllowed(const MinimapScene& scene);
  // The enemies the minimap marks now: none while minimap_enemies is off.
  [[nodiscard]] std::vector<ui::MinimapMarker> minimapEnemyMarkers();
  // The items lying on the ground the minimap marks now: none while minimap_items is off.
  [[nodiscard]] std::vector<ui::MinimapMarker> minimapItemMarkers() const;
  // Shows the story lock's message in the dialog, unless the dialog is on screen already.
  // Opens the island map over the field, with the maps the player has visited.
  void openWorldMap();
  void showLockNotice(const std::string& textKey);
  // The pause menu's quick save (docs/architecture/guardar-y-cargar.md, 3.8): writes the game to
  // its home slot, or the first empty manual slot for a playthrough that never saved; a notice
  // when there is no room or the write fails. The save indicator follows the write itself.
  // Whether the game was written.
  bool quickSave();
  // The bulk trades (bulk_amount.hpp), on the trader's screen, the card shop and the pause menu.
  void setBulkTrades(bool enabled);

  PlayContext context_;
  GameStart game_;
  std::unique_ptr<FieldScene> scene_;
  std::optional<assets::DigimonTable> species_;
  FreeExploration freeExploration_;
  FieldBattleGround battleGround_;
  BattleScreen battleScreen_;
  StorySceneGlue storyScenes_;
  // The naming box the story's renaming opens, when the ruleset has the new game's scene.
  std::unique_ptr<NamingBox> namingBox_;
  FeedScene feedScene_;
  FieldToilet fieldToilet_;
  FieldPoop fieldPoop_;
  FieldStoryProps storyProps_;
  BossArrivalScene bossArrival_;
  PartnerMenu partnerMenu_;
  // The evolution requirements on the pause menu's chart (an engine improvement).
  ChartRequirements chartRequirements_;
  FieldHud fieldHud_;
  // The enemies on the minimap (an engine improvement, minimap_enemies): whether they show and
  // the fighting sections of the map's script.
  bool minimapEnemiesShown_ = true;
  std::optional<MinimapEnemyFinder> minimapEnemies_;
  // Whether the minimap marks the items on the ground (an engine improvement, minimap_items).
  bool minimapItemsShown_ = true;
  // The shop job's progress bar (an engine improvement) and what it last published.
  ShopJobBar shopJobBar_;
  std::optional<ShopJobBarView> publishedShopJob_;
  SleepScreen sleepScreen_;
  SaveMachine saveMachine_;
  // The story's textboxes, when the game runs the story.
  std::optional<StoryTextbox> storyTextbox_;
  std::unique_ptr<StoryFieldLink> storyLink_;
  EvolutionScene evolutionScene_;
  DeathScene deathScene_;
  EndingScene endingScene_;
  EggBoxScreen eggBoxScreen_;
  GymScreen gymScreen_;
  ShopScreen shopScreen_;
  ItemKeeperScreen itemKeeperScreen_;
  DiscardScreen discardScreen_;
  JukeboxScreen jukeboxScreen_;
  TournamentScheduleScreen scheduleScreen_;
  TournamentBracketScreen bracketScreen_;
  TournamentMatchScreen matchScreen_;
  TradeScreen tradeScreen_;
  BirdTransportScreen birdScreen_;
  WorldMapScreen worldMapScreen_;
  // The rain's and the wind's sound under the weather (an engine improvement).
  std::unique_ptr<WeatherAmbience> weatherAmbience_;
  // The season last published to the menus (SeasonNameSetting).
  std::optional<std::string_view> seasonShown_;
  CardShopScreen cardShopScreen_;
  MeritShopScreen meritShopScreen_;
  MoneyBoxScreen moneyBoxScreen_;
  FieldItemsScreen fieldItemsScreen_;
  // The numbers over the partner the story's deal_damage shows on the field.
  std::optional<FieldNumbers> fieldNumbers_;
  AwardScreen awardScreen_;
  PraiseScoldCall praiseScold_;
  FishingScreen fishingScreen_;
  CurlingScreen curlingScreen_;
  std::uint32_t mapSerial_ = 0;
  // Whether a battle ran last frame: its end may change the bag, the money and the partner.
  bool battleRanLastFrame_ = false;
  bool evolutionRanLastFrame_ = false;
  // What hud.minimap_allowed last said (minimap_setting.hpp: minimapAllowed); nothing before the
  // first frame publishes it.
  std::optional<bool> minimapAllowed_;
  // Whether a command changed what the script warps' guards read (the partner's menu).
  bool guardFactsChanged_ = false;
  // Whether this tick gave the field the player's walk (tick).
  bool fieldWalks_ = false;
  // Whether the dialog on screen asks to go back to the title screen.
  bool toTitleAsked_ = false;
  bool toTitle_ = false;
  // Whether the dialog on screen asks to save and quit the game.
  bool quitAsked_ = false;
  bool quitGame_ = false;
  // Whether the dialog on screen is the story lock's message.
  bool lockNoticeShown_ = false;
  std::optional<MovieMoment> movieRequest_;
  // Built once the HUD's settings are in, before its document is shown.
  std::optional<SaveIndicator> saveIndicator_;
  // The last save of a free field (keepFieldSnapshot) and the real seconds since it was taken.
  std::optional<game::save::SaveFile> fieldSnapshot_;
  double snapshotSeconds_ = 0.0;
};

} // namespace dwe::app
