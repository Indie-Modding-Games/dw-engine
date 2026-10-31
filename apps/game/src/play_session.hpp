#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/debug/debug_ui.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/render/renderer.hpp>
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
#include "boss_arrival_scene.hpp"
#include "bug_report.hpp"
#include "care_commands.hpp"
#include "curling_screen.hpp"
#include "death_scene.hpp"
#include "egg_box_screen.hpp"
#include "evolution_scene.hpp"
#include "feed_scene.hpp"
#include "field_hud.hpp"
#include "field_items_screen.hpp"
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
#include "movie_rules.hpp"
#include "naming_box.hpp"
#include "options.hpp"
#include "partner_menu.hpp"
#include "praise_scold_call.hpp"
#include "save_command.hpp"
#include "save_machine.hpp"
#include "session_start.hpp"
#include "shop_screen.hpp"
#include "sleep_screen.hpp"
#include "state_trace.hpp"
#include "story_field_link.hpp"
#include "story_scene_glue.hpp"
#include "story_textbox.hpp"
#include "tournament_schedule_screen.hpp"
#include "user_settings.hpp"

namespace dwe::app {

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
  std::optional<std::string> evolution;
  CareCommands care;
  SaveCommand save;
  bool saveMachine = false;
  std::optional<std::string> gym;
  // place("<who> <x> <z> <rotation>") commands, in order (place_command.hpp).
  std::vector<std::string> placements;
  // story_set("trigger <n> on|off" | "var <n> <value>") commands, in order (story_set_command.hpp).
  std::vector<std::string> storyWrites;

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

  // One logic tick: the battle, or the field and the game time unless a menu pauses them.
  void tick(const game::field::FieldInput& intent, const FishingButtons& fishing);

  // Takes the UI's commands for the game (the battle, free exploration, the partner's menu); false
  // for any other.
  bool handle(const ui::UiCommand& command);

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
  // The item keeper's, the discard menu's and the trade menu's buttons the menus do not navigate
  // with (move by tens, move all, the jump to the ends, the description).
  void pressItemKeeper(const input::InputState& input);
  // The partner menu's buttons the menus do not navigate with: `extra` opens the tech help.
  void pressPartnerMenu(const input::InputState& input);
  // The stones game's arrows and confirm button, while it runs.
  void pressCurling(const input::InputState& input) { curlingScreen_.press(input); }
  // What the keyboard typed, for the story's naming box while it is open (takesText).
  [[nodiscard]] bool takesText() const;
  // Whether the menu button belongs to the game rather than opening the menu: a naming box takes
  // it, and a gym course takes it as the original's cancel.
  [[nodiscard]] bool holdsMenu() const;
  void type(std::string_view text, bool enter);

  // Where the characters and the battle's fighters stand after this tick, and the buttons held in
  // it, for --trace (state_trace.hpp).
  [[nodiscard]] TraceFrame traceFrame(std::uint64_t tick, const input::InputState& input) const;

  // Saves the game to a slot (AppFlow, for a new game's slot).
  [[nodiscard]] core::Result<void> saveTo(game::save::SlotId slot) const {
    return game_.session->save(slot);
  }

  // Whether the player chose to go back to the title screen, once (docs/architecture/
  // guardar-y-cargar.md, 3.8).
  [[nodiscard]] bool takeToTitle() { return std::exchange(toTitle_, false); }

  // The partner's status as the status page shows it (partner.* settings).
  [[nodiscard]] screens::ScreenSettings partnerStatus() const;
  // The story's triggers and variables, when the game runs the story (autotest's story.* settings).
  [[nodiscard]] const game::story::StoryState* storyState() const;
  [[nodiscard]] std::string_view partnerSpecies() const {
    return game_.session->partner().state().species;
  }

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

private:
  [[nodiscard]] bool paused() const;
  // What keeps the game from being saved as it is now (a battle, a script, a scene), or empty when
  // the field is free.
  [[nodiscard]] std::string_view busyWith() const;
  // Keeps a snapshot of the game while the field is free, every few seconds, for a bug report
  // made while it is not.
  void keepFieldSnapshot(double frameSeconds);
  // The sulking partner's butterfly (butterfly.c), shown or taken away.
  void showButterfly(bool partnerNormal, const session::GameSession& session);
  // Shows the story lock's message in the dialog, unless the dialog is on screen already.
  void showLockNotice(const std::string& textKey);

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
  FieldHud fieldHud_;
  SleepScreen sleepScreen_;
  SaveMachine saveMachine_;
  // The story's textboxes, when the game runs the story.
  std::optional<StoryTextbox> storyTextbox_;
  std::unique_ptr<StoryFieldLink> storyLink_;
  EvolutionScene evolutionScene_;
  DeathScene deathScene_;
  EggBoxScreen eggBoxScreen_;
  GymScreen gymScreen_;
  ShopScreen shopScreen_;
  ItemKeeperScreen itemKeeperScreen_;
  DiscardScreen discardScreen_;
  JukeboxScreen jukeboxScreen_;
  TournamentScheduleScreen scheduleScreen_;
  TradeScreen tradeScreen_;
  BirdTransportScreen birdScreen_;
  CardShopScreen cardShopScreen_;
  MeritShopScreen meritShopScreen_;
  FieldItemsScreen fieldItemsScreen_;
  AwardScreen awardScreen_;
  PraiseScoldCall praiseScold_;
  FishingScreen fishingScreen_;
  CurlingScreen curlingScreen_;
  std::uint32_t mapSerial_ = 0;
  // Whether a battle ran last frame: its end may change the bag, the money and the partner.
  bool battleRanLastFrame_ = false;
  // Whether a command changed what the script warps' guards read (the partner's menu).
  bool guardFactsChanged_ = false;
  // Whether the dialog on screen asks to go back to the title screen.
  bool toTitleAsked_ = false;
  bool toTitle_ = false;
  // Whether the dialog on screen is the story lock's message.
  bool lockNoticeShown_ = false;
  std::optional<MovieMoment> movieRequest_;
  // The last save of a free field (keepFieldSnapshot) and the real seconds since it was taken.
  std::optional<game::save::SaveFile> fieldSnapshot_;
  double snapshotSeconds_ = 0.0;
};

} // namespace dwe::app
