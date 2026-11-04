#pragma once

#include <dwe/ui/ui.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// HD scenery (an engine improvement; docs/modulos/apps/game.md, «Escenarios en alta definición»):
// the field's prerendered background and its depth layers drawn smooth at the window's resolution
// (render::MapRenderer::setHdScenery) instead of the original's pixels. A display setting, saved
// with the user's settings, and the command with the same name that the options page and the
// title's options send: true (the default) or false. It changes no rule of the game: the camera,
// collisions, triggers and every position stay in the original's units.
inline constexpr std::string_view HdScenerySetting = "hd_scenery";

// Whether HD scenery is on at start: the saved choice, or on.
[[nodiscard]] bool startHdScenery(const UserSettings& settings);

// Changes HD scenery while the game runs and saves it with the other settings (settings.yaml).
class HdSceneryChanger {
public:
  // settingsFile is where the choice is saved; nothing for runs that must not touch the user's
  // settings (autotests). The settings must outlive the changer.
  HdSceneryChanger(UserSettings& settings, std::optional<std::filesystem::path> settingsFile);

  // Takes the value of an hd_scenery command (a boolean; anything else changes nothing) and
  // returns whether HD scenery is on now.
  bool change(const ui::UiValue& value);

  [[nodiscard]] bool on() const { return startHdScenery(*settings_); }

private:
  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
};

} // namespace dwe::app
