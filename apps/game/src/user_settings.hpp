#pragma once

#include <dwe/core/error.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

// What the player chose in the menus and the game keeps between runs, in settings.yaml in the user's data folder.
struct UserSettings {
    // The language of the texts ("en", "es"); the system's when not chosen yet.
    std::optional<std::string> language;
    // The font of the original's text (text_font): kept for the font option, which reads and writes it.
    std::optional<std::string> textFont;
};

// Reads the settings. Keys it does not know are left out, so an older game reads a newer file.
core::Result<UserSettings> parseUserSettings(std::string_view yaml);

// Writes the settings as YAML.
core::Result<std::string> formatUserSettings(const UserSettings& settings);

// The file the settings are kept in, or nothing (logged) when the system gives no data folder.
std::optional<std::filesystem::path> userSettingsFile();

// Reads the settings from a file: the defaults when it does not exist, and (logged) when it cannot be read.
UserSettings loadUserSettings(const std::filesystem::path& file);

// Writes the settings to a file, logging a failure.
void saveUserSettings(const std::filesystem::path& file, const UserSettings& settings);

} // namespace dwe::app
