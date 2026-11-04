#pragma once

#include <dwe/ui/ui.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// Whether the HUD shows the round minimap (an engine improvement; the original has none): a UI
// setting, saved with the user's settings, and the command with the same name that the options
// page, the pause menu's minimap page and the title's options send. `on` (the default, whatever the
// skin) shows it and `off` hides it. It changes no rule of the game.
inline constexpr std::string_view MinimapSetting = "minimap";
inline constexpr std::string_view MinimapOn = "on";
inline constexpr std::string_view MinimapOff = "off";
// The value older settings saved for "as the skin decides", read as on: both skins now draw it.
inline constexpr std::string_view MinimapAuto = "auto";
// The boolean setting and command it replaces: true is on and false is off.
inline constexpr std::string_view LegacyMinimapVisibleSetting = "minimap_visible";

// The minimap mode a value of either setting means: off as off, a boolean as on or off, anything
// else (unset and the old auto included) as on.
[[nodiscard]] std::string_view minimapModeOf(const ui::UiValue& value);

// The minimap mode the run starts with: the saved one, or on when none is saved. A saved value that
// is not a mode (nor the old auto) is logged and starts on.
[[nodiscard]] std::string_view startMinimapMode(const std::optional<std::string>& saved);

// What the field is doing, for whether the minimap may show.
struct MinimapScene {
  // Game time runs: the player walks the field, with no menu, scene, talk, fade or map change
  // (the original's clock and bars slide away otherwise: clock.c: renderGameClock).
  bool fieldRunning = false;
  bool battle = false;
  // A minigame or a screen of its own: a training course, fishing, curling, a tournament's
  // screens, an evolution or a death scene.
  bool minigame = false;
};

// Whether the minimap may show (the setting hud.minimap_allowed): only while the field runs, as
// the original's clock and bars, and never in a battle or a minigame, whose own HUDs take the
// screen. The player's setting still decides whether it shows at all.
[[nodiscard]] bool minimapAllowed(const MinimapScene& scene);
inline constexpr std::string_view MinimapAllowedSetting = "hud.minimap_allowed";

// Changes the player's minimap mode while the game runs and saves it with the other settings
// (settings.yaml).
class MinimapChanger {
public:
  // settingsFile is where the choice is saved; nothing for runs that must not touch the user's
  // settings (autotests). The settings must outlive the changer.
  MinimapChanger(UserSettings& settings, std::optional<std::filesystem::path> settingsFile);

  // Takes the value of a minimap command (or of the old boolean one) and returns the mode to
  // publish.
  std::string_view change(const ui::UiValue& value);

private:
  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
};

// The setting that hides the HUD's minimap while a window lies over it (MinimapCover): the HUD
// puts the class "covered" on #minimap, which keeps its place but draws nothing.
inline constexpr std::string_view MinimapCoveredSetting = "hud.minimap_covered";
// The HUD's document and its minimap's panel.
inline constexpr std::string_view HudDocument = "hud";
inline constexpr std::string_view MinimapPanel = "minimap";

// Keeps the HUD's minimap from showing over a window, whatever the skin, the window size and the
// minimap's size (a dwe rule: the original has no minimap). While anything another shown document
// draws (Ui::drawnRects: the pause menu, a shop's or a trader's window, a summary box) lies over
// the minimap's panel, the minimap hides; it shows again once nothing has lain there for
// HoldSeconds, so a box that closes under a transform, which drawnRects leaves out, does not
// uncover it while it shrinks.
class MinimapCover {
public:
  static constexpr double HoldSeconds = 0.25;

  // Whether the minimap hides now: `minimap` is its panel's box (nothing, or an empty box, when the
  // HUD does not lay it out), `drawn` what the other documents draw, `seconds` the time since the
  // last call.
  bool update(const std::optional<ui::Ui::Rect>& minimap, std::span<const ui::Ui::Rect> drawn,
              double seconds);
  // The same from the UI's last layout, publishing MinimapCoveredSetting when it changes.
  void update(ui::Ui& ui, double seconds);

  [[nodiscard]] bool covered() const { return covered_; }

private:
  bool covered_ = false;
  // How long nothing has lain over the minimap while it hides.
  double clearSeconds_ = 0.0;
  std::optional<bool> published_;
};

} // namespace dwe::app
