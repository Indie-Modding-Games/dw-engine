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

#include "autotest.hpp"
#include "battle_screen.hpp"
#include "care_commands.hpp"
#include "field_hud.hpp"
#include "field_scene.hpp"
#include "frame_stats.hpp"
#include "free_exploration.hpp"
#include "options.hpp"
#include "partner_menu.hpp"
#include "save_command.hpp"
#include "session_start.hpp"
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

  void bind(script::ScriptVm& scripts);
};

// What lives as long as one game (docs/architecture/guardar-y-cargar.md, 3.1): the session, the
// field, the battle, the partner's menu and the HUD. It is built whole from a new game or a save
// and torn down whole.
class PlaySession {
public:
  static core::Result<std::unique_ptr<PlaySession>> create(const PlayContext& context);

  PlaySession(const PlaySession&) = delete;
  PlaySession& operator=(const PlaySession&) = delete;
  PlaySession(PlaySession&&) = delete;
  PlaySession& operator=(PlaySession&&) = delete;
  ~PlaySession();

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

  // The partner's status as the status page shows it (partner.* settings).
  [[nodiscard]] screens::ScreenSettings partnerStatus() const;

private:
  PlaySession(const PlayContext& context, GameStart game, std::unique_ptr<FieldScene> scene);

  [[nodiscard]] bool paused() const;

  PlayContext context_;
  GameStart game_;
  std::unique_ptr<FieldScene> scene_;
  std::optional<assets::DigimonTable> species_;
  FreeExploration freeExploration_;
  BattleScreen battleScreen_;
  PartnerMenu partnerMenu_;
  FieldHud fieldHud_;
  std::uint32_t mapSerial_ = 0;
  // Whether a battle ran last frame: its end may change the bag, the money and the partner.
  bool battleRanLastFrame_ = false;
  // Whether a command changed what the script warps' guards read (the partner's menu).
  bool guardFactsChanged_ = false;
};

} // namespace dwe::app
