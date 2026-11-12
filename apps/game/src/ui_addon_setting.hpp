#pragma once

#include <dwe/ui/ui.hpp>
#include <dwe/ui/ui_addon.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "user_settings.hpp"

namespace dwe::app {

// The UI addons (docs/modding/interfaces.md): the whole UIs under ui/addons that the player picks
// in Opciones -> Interfaz, over the game's own (ui/addon.yaml).
//
// The settings the documents read: the UI on (ui_addon, "default" for the game's own), the ones
// offered (ui_addons, ids separated by spaces, the game's own first), each one's name
// (ui_addon.name.<id>), whether the run is in safe mode (ui_addon.safe: the game's own UI only) and
// why the last pick could not go on (ui_addon.problem, empty when it could). The ui_addon command
// picks one; ui_addon_reload reads the one on again from its files.
inline constexpr std::string_view UiAddonSetting = "ui_addon";
inline constexpr std::string_view UiAddonsSetting = "ui_addons";
inline constexpr std::string_view UiAddonNamePrefix = "ui_addon.name.";
inline constexpr std::string_view UiAddonSafeSetting = "ui_addon.safe";
inline constexpr std::string_view UiAddonProblemSetting = "ui_addon.problem";
inline constexpr std::string_view UiAddonReloadCommand = "ui_addon_reload";
// The plugins (type plugin): documents over whichever UI is on, all on or all off (ui_plugins, on
// unless turned off), and how many the packs offer (ui_plugins.count).
inline constexpr std::string_view UiPluginsSetting = "ui_plugins";
inline constexpr std::string_view UiPluginsCountSetting = "ui_plugins.count";
// The notice that the game's own UI came back, and why; it hides by itself.
inline constexpr std::string_view UiNoticeDocument = "ui_notice";
inline constexpr double UiNoticeSeconds = 8.0;

// The UI the run starts with: the game's own in safe mode (--safe-ui, or Shift held at start) or
// when nothing was picked; else the saved pick, which may no longer be offered (use() then says
// so).
[[nodiscard]] std::string startUiAddon(bool safe, const std::optional<std::string>& saved);

// Whether the game takes a command from an addon's documents: the commands and options its UI
// contract declares (docs/modding/ui-api.md), no more.
[[nodiscard]] bool addonMaySend(std::string_view command);

// The player's UI addon while the game runs: offered, put on, saved, and the game's own UI back
// with a notice when one cannot go on.
class UiAddonChanger {
public:
  // found: the packs' addons (ui::findUiAddons); the ones that do not read are logged and not
  // offered. settingsFile is where the pick is saved; nothing for runs that must not touch the
  // user's settings. Everything given must outlive the changer.
  UiAddonChanger(ui::Ui& ui, UserSettings& settings,
                 std::optional<std::filesystem::path> settingsFile,
                 std::vector<ui::FoundUiAddon> found, bool safe);

  // Puts on the UI the run starts with and publishes the settings. The game's documents must be
  // loaded.
  void start();
  // The ui_addon, ui_plugins and ui_addon_reload commands; false for any other.
  bool handle(const ui::UiCommand& command);
  // Puts on a pick once no modal screen is shown (the options page itself may be the addon's; a
  // plugin's document must not take the menu's focus), and hides the notice when its time is up.
  // Every frame.
  void update(double seconds);

  // The ids offered, the game's own first.
  [[nodiscard]] std::span<const std::string> offered() const { return offered_; }
  // The plugins the packs offer.
  [[nodiscard]] std::span<const ui::UiAddon> plugins() const { return plugins_; }

private:
  void useUi(const std::string& id);
  void usePlugins(bool on);
  void addProblem(std::string problem);
  void finish();
  void publish();
  [[nodiscard]] const ui::UiAddon* find(std::string_view id) const;

  ui::Ui* ui_;
  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
  std::vector<ui::UiAddon> addons_;
  std::vector<ui::UiAddon> plugins_;
  std::vector<std::string> offered_;
  bool safe_;
  bool pluginsOn_ = false;
  std::optional<std::string> pending_;
  std::optional<bool> pendingPlugins_;
  std::string problem_;
  double noticeLeft_ = 0.0;
};

} // namespace dwe::app
