#pragma once

#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// The field's enhanced graphics (docs/architecture/render.md, «Paso de luz y color»): three engine
// options that change how the field looks and no rule of the game, so all are on by default. Each
// is a UI setting and the command, with the same name, that the pause menu's and the title's
// options send; the game applies it at once and saves it (settings.yaml).
//
// Shadows: off (none), round (the original's round shadow under every character:
// drop_shadow.c: renderDropShadow) or sun (the round one and a soft one the sun casts, longer
// and turning with the game's clock, gone at night and indoors).
inline constexpr std::string_view ShadowsSetting = "shadows";
// Enhanced lighting (true or false): the time of day's tint and the maps' point lights.
inline constexpr std::string_view EnhancedLightingSetting = "enhanced_lighting";
// Vivid colours (true or false): vibrance, a gentle contrast and a soft bloom.
inline constexpr std::string_view VividColorsSetting = "vivid_colors";

enum class ShadowMode : std::uint8_t { Off, Round, Sun };

[[nodiscard]] std::optional<ShadowMode> parseShadowMode(std::string_view name);
[[nodiscard]] std::string_view shadowModeName(ShadowMode mode);

struct GraphicsOptions {
  ShadowMode shadows = ShadowMode::Sun;
  bool enhancedLighting = true;
  bool vividColors = true;

  friend bool operator==(const GraphicsOptions&, const GraphicsOptions&) = default;
};

// The options a run starts with: the saved ones, the defaults for those not saved. A saved shadow
// mode that is not one is logged and starts with the default.
[[nodiscard]] GraphicsOptions startGraphicsOptions(const UserSettings& settings);

// The options with a command line's --graphics on top: a comma-separated list of
// shadows=off|round|sun, lighting=on|off and colors=on|off. Nothing when the text is not one.
[[nodiscard]] std::optional<GraphicsOptions> withGraphicsFlag(GraphicsOptions options,
                                                              std::string_view text);

// Changes the options while the game runs, publishes them to the UI and saves them with the other
// settings.
class GraphicsChanger {
public:
  // settingsFile is where the choices are saved; nothing for runs that must not touch the user's
  // settings (autotests). The settings must outlive the changer.
  GraphicsChanger(UserSettings& settings, std::optional<std::filesystem::path> settingsFile,
                  GraphicsOptions start);

  [[nodiscard]] const GraphicsOptions& options() const { return options_; }

  // Takes a command; returns whether it was one of the three (a value of the wrong kind or an
  // unknown mode changes nothing but is still taken), and publishes what is in use.
  bool handle(const ui::UiCommand& command, ui::Ui& ui);
  // Publishes the three settings.
  void publish(ui::Ui& ui) const;

private:
  void save();

  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
  GraphicsOptions options_;
};

} // namespace dwe::app
