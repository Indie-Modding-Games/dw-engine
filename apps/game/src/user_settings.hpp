#pragma once

#include <dwe/core/error.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

// What the player chose in the menus and the game keeps between runs, in settings.yaml in the
// user's data folder.
struct UserSettings {
  // The language of the texts ("en", "es"); the system's when not chosen yet.
  std::optional<std::string> language;
  // The font of "original look" text (text_font: modern, original); the packs' default when not
  // chosen yet.
  std::optional<std::string> textFont;
  // The skin of the UI (ui_skin: the id of a skin the packs offer); the packs' default when not
  // chosen yet.
  std::optional<std::string> uiSkin;
  // Whether the HUD shows the minimap (minimap: auto, on, off); auto when not chosen yet.
  std::optional<std::string> minimap;
  // Free exploration (free_exploration: true, false): the scripts' map changes go anywhere they
  // can, whatever the story; off when not chosen yet.
  std::optional<bool> freeExploration = std::nullopt;
  // Smooth motion (smooth_motion: true, false): the characters and the camera are drawn gliding
  // between the original's 20 Hz frames; on when not chosen yet.
  std::optional<bool> smoothMotion = std::nullopt;
  // The save indicator (save_notice: true, false): a small figure and "Saving" in a corner while
  // the game saves; on when not chosen yet.
  std::optional<bool> saveNotice = std::nullopt;
  // The time in digits (digital_time: true, false): a small box with the game time as HH:MM under
  // the field HUD's dial clock; on when not chosen yet.
  std::optional<bool> digitalTime = std::nullopt;
  // Bulk trades (bulk_trades: true, false): the item trader and the card shop's packs ask how many
  // at once; on when not chosen yet.
  std::optional<bool> bulkTrades = std::nullopt;
  // The evolution requirements on the chart (evolution_requirements: true, false); on when not
  // chosen yet.
  std::optional<bool> evolutionRequirements = std::nullopt;
  // Translated signs (translated_signs: true, false): the signs painted in the maps are written in
  // the player's language; on when not chosen yet.
  std::optional<bool> translatedSigns = std::nullopt;
  // The status page's extras (status_extras: true, false): the partner's tiredness bar and its care
  // mistakes, which the original keeps hidden; on when not chosen yet.
  std::optional<bool> statusExtras = std::nullopt;
  // The canonical stages (canonical_stages: true, false): the chart names the stages as the series
  // does (Digi-Egg to Ultra) and draws a Mega box; off when not chosen yet.
  std::optional<bool> canonicalStages = std::nullopt;
  // What walks the player in the field (walk_control: keys, mouse; walk_control.hpp): the keyboard
  // and the pad, or the mouse, never both; the keys when not chosen yet.
  std::optional<std::string> walkControl = std::nullopt;
  // Long names (long_names: true, false): the naming box takes names of up to LongNameLength
  // letters (naming_box.hpp) instead of the ruleset's six; on when not chosen yet.
  std::optional<bool> longNames = std::nullopt;
  // The shop job's progress bar (shop_job_bar: true, false; shop_job_bar.hpp): the canyon shop's
  // tally against the join while its job runs; on when not chosen yet.
  std::optional<bool> shopJobBar = std::nullopt;
  // Continuous music (continuous_music: true, false; session::SessionMusic::setContinuousMusic): a
  // map whose song is the one playing lets it go on instead of playing it again from its start; on
  // when not chosen yet.
  std::optional<bool> continuousMusic = std::nullopt;
  // The need bubble with several needs at once (need_bubble_stack: true, false; ui/hud): up to
  // three of the partner's needs side by side in one balloon, most urgent first, instead of the
  // original's two taking turns; on when not chosen yet.
  std::optional<bool> needBubbleStack = std::nullopt;
  // Whether M opens the island map (world_map: true, false; world_map.hpp); on when not chosen.
  std::optional<bool> worldMap = std::nullopt;
};

// Reads the settings. Keys it does not know are left out, so an older game reads a newer file.
core::Result<UserSettings> parseUserSettings(std::string_view yaml);

// Writes the settings as YAML.
core::Result<std::string> formatUserSettings(const UserSettings& settings);

// The file the settings are kept in, or nothing (logged) when the system gives no data folder.
std::optional<std::filesystem::path> userSettingsFile();

// Reads the settings from a file: the defaults when it does not exist, and (logged) when it cannot
// be read.
UserSettings loadUserSettings(const std::filesystem::path& file);

// Writes the settings to a file, logging a failure.
void saveUserSettings(const std::filesystem::path& file, const UserSettings& settings);

} // namespace dwe::app
