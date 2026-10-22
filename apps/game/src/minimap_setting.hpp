#pragma once

#include <dwe/ui/ui.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// Whether the HUD shows the minimap: a UI setting and the pause menu's command with the same name. `auto` leaves it
// to the skin (the modern one shows it; the original game has no minimap, so the original-look skin hides it), `on`
// shows it and `off` hides it.
inline constexpr std::string_view MinimapSetting = "minimap";
inline constexpr std::string_view MinimapAuto = "auto";
inline constexpr std::string_view MinimapOn = "on";
inline constexpr std::string_view MinimapOff = "off";
// The boolean setting and command it replaces: true is on and false is off.
inline constexpr std::string_view LegacyMinimapVisibleSetting = "minimap_visible";

// The minimap mode a value of either setting means: a known mode as it is, a boolean as on or off, anything else
// (unset included) as auto.
[[nodiscard]] std::string_view minimapModeOf(const ui::UiValue& value);

// The minimap mode the run starts with: the saved one, or auto when none is saved. A saved value that is not a mode is
// logged and starts on auto.
[[nodiscard]] std::string_view startMinimapMode(const std::optional<std::string>& saved);

// Changes the player's minimap mode while the game runs and saves it with the other settings (settings.yaml).
class MinimapChanger {
public:
    // settingsFile is where the choice is saved; nothing for runs that must not touch the user's settings
    // (autotests). The settings must outlive the changer.
    MinimapChanger(UserSettings& settings, std::optional<std::filesystem::path> settingsFile);

    // Takes the value of a minimap command (or of the old boolean one) and returns the mode to publish.
    std::string_view change(const ui::UiValue& value);

private:
    UserSettings* settings_;
    std::optional<std::filesystem::path> settingsFile_;
};

} // namespace dwe::app
