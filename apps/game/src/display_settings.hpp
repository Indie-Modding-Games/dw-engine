#pragma once

#include <dwe/ui/ui.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// The options that set up the screen rather than a game (pause_menu/logic.lua: display_entries,
// and the minimap page's size): UI settings with the same name as the commands the options page,
// the title's options and the minimap page send, saved with the user's settings like the rest.
inline constexpr std::string_view FullscreenSetting = "fullscreen";
inline constexpr std::string_view VsyncSetting = "vsync";
inline constexpr std::string_view UiScaleSetting = "ui_scale";
inline constexpr std::string_view MinimapSizeSetting = "minimap_size";
// The minimap's sizes (hud/presenter.lua); medium when not chosen.
inline constexpr std::string_view MinimapSizeSmall = "small";
inline constexpr std::string_view MinimapSizeMedium = "medium";
inline constexpr std::string_view MinimapSizeLarge = "large";

// What the run starts with: the window mode and vsync of the command line when it asks for them
// (--fullscreen, --no-vsync), the saved ones otherwise; the saved UI scale (ui::Ui keeps it within
// its range) and the saved minimap size, or their defaults.
struct DisplayStart {
  bool fullscreen = false;
  bool vsync = true;
  float uiScale = 1.0F;
  std::string_view minimapSize = MinimapSizeMedium;
};
[[nodiscard]] DisplayStart startDisplay(bool fullscreenFlag, bool vsyncFlag,
                                        const UserSettings& settings);

// The minimap size a value means: one of the sizes, or medium for anything else.
[[nodiscard]] std::string_view minimapSizeOf(const ui::UiValue& value);

// Keeps the player's display choices in the settings as they change and saves them (settings.yaml).
class DisplayChanger {
public:
  // settingsFile is where the choices are saved; nothing for runs that must not touch the user's
  // settings (autotests, the control channel). The settings must outlive the changer.
  DisplayChanger(UserSettings& settings, std::optional<std::filesystem::path> settingsFile);

  // Each keeps a choice the game applied (the window's real mode, the UI's clamped scale).
  void keepFullscreen(bool fullscreen);
  void keepVsync(bool vsync);
  void keepUiScale(float scale);
  // Takes the value of a minimap_size command and returns the size to publish.
  std::string_view changeMinimapSize(const ui::UiValue& value);

private:
  void save() const;

  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
};

} // namespace dwe::app
