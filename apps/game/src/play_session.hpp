#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/debug/debug_ui.hpp>
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
#include <utility>

#include "app_flow.hpp"
#include "autotest.hpp"
#include "battle_screen.hpp"
#include "care_commands.hpp"
#include "evolution_scene.hpp"
#include "field_hud.hpp"
#include "field_scene.hpp"
#include "frame_stats.hpp"
#include "free_exploration.hpp"
#include "movie_rules.hpp"
#include "options.hpp"
#include "partner_menu.hpp"
#include "save_command.hpp"
#include "save_machine.hpp"
#include "session_start.hpp"
#include "sleep_screen.hpp"
#include "story_field_link.hpp"
#include "story_textbox.hpp"
#include "story_scene_glue.hpp"
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

// The console's and the scripts' commands on the game being played (give_item, use_item, battle,
// evolve, the care commands and save). The script VM outlives the games, so they are bound once
// and queued here; the game being played applies them between frames.
struct SessionCommands {
  std::optional<std::string> gift;
  std::optional<std::string> use;
  std::optional<std::string> battle;
  std::optional<std::string> evolution;
  CareCommands care;
  SaveCommand save;
  bool saveMachine = false;

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
  void tick(const game::field::FieldInput& intent);

  // Takes the UI's commands for the game (the battle, free exploration, the partner's menu); false
  // for any other.
  bool handle(const ui::UiCommand& command);

  // Once per frame after the commands: runs the queued session commands, follows the map changes
  // and publishes the HUD. An autotest hears of map changes and failed saves.
  void update(SessionCommands& commands, Autotest* autotest, FrameStats& stats, double frameSeconds,
              float alpha);

  // How much the map change's or the sleep's fade takes away from the screen, from 0 to 1.
  [[nodiscard]] float fadeLevel() const;
  // Feeds the story's textboxes the player's keys, once per tick while no menu takes them.
  void pressStory(const input::InputState& input);

  // Saves the game to a slot (AppFlow, for a new game's slot).
  [[nodiscard]] core::Result<void> saveTo(game::save::SlotId slot) const {
    return game_.session->save(slot);
  }

  // Whether the player chose to go back to the title screen, once (docs/architecture/
  // guardar-y-cargar.md, 3.8).
  [[nodiscard]] bool takeToTitle() { return std::exchange(toTitle_, false); }

  // The partner's status as the status page shows it (partner.* settings).
  [[nodiscard]] screens::ScreenSettings partnerStatus() const;

  // The movie the game asks the app for (main.c: main): ED2 once a game saved after the ending is
  // loaded (MAIN_STATE 2), EDR once the ending ends the game (MAIN_STATE 3). The ending's routines
  // 53 and 54 (HIST-S7) ask for them. Taking the request clears it.
  void requestMovie(MovieMoment moment) { movieRequest_ = moment; }
  [[nodiscard]] std::optional<MovieMoment> takeMovieRequest() {
    return std::exchange(movieRequest_, std::nullopt);
  }

private:
  [[nodiscard]] bool paused() const;

  PlayContext context_;
  GameStart game_;
  std::unique_ptr<FieldScene> scene_;
  std::optional<assets::DigimonTable> species_;
  FreeExploration freeExploration_;
  BattleScreen battleScreen_;
  StorySceneGlue storyScenes_;
  PartnerMenu partnerMenu_;
  FieldHud fieldHud_;
  SleepScreen sleepScreen_;
  SaveMachine saveMachine_;
  // The story's textboxes, when the game runs the story.
  std::optional<StoryTextbox> storyTextbox_;
  std::unique_ptr<StoryFieldLink> storyLink_;
  EvolutionScene evolutionScene_;
  std::uint32_t mapSerial_ = 0;
  // Whether a battle ran last frame: its end may change the bag, the money and the partner.
  bool battleRanLastFrame_ = false;
  // Whether a command changed what the script warps' guards read (the partner's menu).
  bool guardFactsChanged_ = false;
  // Whether the dialog on screen asks to go back to the title screen.
  bool toTitleAsked_ = false;
  bool toTitle_ = false;
  std::optional<MovieMoment> movieRequest_;
};

} // namespace dwe::app
