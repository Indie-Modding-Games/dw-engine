#pragma once

#include <dwe/ui/ui.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// The title screen's options (docs/modulos/apps/game.md, «Opciones en partida»): the main menu's
// Options entry opens the pause menu's options page over the title, with its tabs, rows and help.
// TitleScreen opens it with these; the page sends the second when it closes there.
inline constexpr std::string_view TitleOptionsCommand = "title_options";
inline constexpr std::string_view TitleOptionsBackCommand = "title_options_back";
// Read by the pause menu as it shows (with pause_menu.start_page): it opens over the title, so its
// game actions (save, back to the title) stay hidden and quit closes the game at once.
inline constexpr std::string_view MenuAtTitleSetting = "pause_menu.at_title";

// An on/off option of the options page that only a running game applies (PlaySession::handle and
// the screens it owns), kept in the user's settings.
struct GameToggle {
  // The UI setting and the page's command, with the same name.
  std::string_view setting;
  std::optional<bool> UserSettings::* saved;
  // Its value while the player has not chosen one.
  bool fallback;
};

// Every such option, in the order of the options page.
[[nodiscard]] std::span<const GameToggle> gameToggles();

// Publishes each option's saved value (or its fallback), so the page shows it before any game.
void publishGameToggles(ui::Ui& ui, const UserSettings& settings);

// Takes one of those options' commands while no game runs: keeps the value in the settings, saves
// them to settingsFile (nothing for runs that must not touch them) and publishes it; the next game
// starts with it. A value that is not true or false changes nothing but is still taken. False for
// any other command.
bool changeGameToggle(const ui::UiCommand& command, ui::Ui& ui, UserSettings& settings,
                      const std::optional<std::filesystem::path>& settingsFile);

} // namespace dwe::app
