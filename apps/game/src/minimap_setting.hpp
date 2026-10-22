#pragma once

#include <dwe/ui/ui.hpp>

#include <string_view>

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

} // namespace dwe::app
