#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <optional>
#include <string>

#include "user_settings.hpp"

namespace dwe::app {

TEST_CASE("the player's settings survive a write and a read, the UI skin with them") {
  const UserSettings settings{.language = "es",
                              .textFont = "original",
                              .uiSkin = "test_contrast",
                              .minimap = "off",
                              .freeExploration = true,
                              .smoothMotion = false,
                              .saveNotice = false,
                              .digitalTime = false,
                              .evolutionRequirements = false,
                              .translatedSigns = false,
                              .statusExtras = false};
  const auto text = formatUserSettings(settings);
  REQUIRE(text.has_value());
  CHECK(text->contains("ui_skin: test_contrast"));
  const auto read = parseUserSettings(*text);
  REQUIRE_MESSAGE(read.has_value(), read.error().message);
  CHECK(read->language == std::optional<std::string>{"es"});
  CHECK(read->textFont == std::optional<std::string>{"original"});
  CHECK(read->uiSkin == std::optional<std::string>{"test_contrast"});
  CHECK(read->minimap == std::optional<std::string>{"off"});
  CHECK(read->freeExploration == std::optional<bool>{true});
  CHECK(read->smoothMotion == std::optional<bool>{false});
  CHECK(read->saveNotice == std::optional<bool>{false});
  CHECK(read->digitalTime == std::optional<bool>{false});
  CHECK(read->evolutionRequirements == std::optional<bool>{false});
  CHECK(read->translatedSigns == std::optional<bool>{false});
  CHECK(read->statusExtras == std::optional<bool>{false});

  // A file from before the setting existed, or without the choice made, leaves it to the packs.
  const auto older = parseUserSettings("language: en\n");
  REQUIRE(older.has_value());
  CHECK_FALSE(older->uiSkin.has_value());
  CHECK_FALSE(older->minimap.has_value());
  CHECK_FALSE(older->freeExploration.has_value());
  CHECK_FALSE(older->smoothMotion.has_value());
  CHECK_FALSE(older->saveNotice.has_value());
  CHECK_FALSE(older->digitalTime.has_value());
  CHECK_FALSE(older->evolutionRequirements.has_value());
  CHECK_FALSE(older->translatedSigns.has_value());
  CHECK_FALSE(older->statusExtras.has_value());
  CHECK_FALSE(formatUserSettings(UserSettings{})->contains("ui_skin"));
}

TEST_CASE("the player's settings file is written and read back") {
  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  CHECK_FALSE(loadUserSettings(file).uiSkin.has_value());
  saveUserSettings(file, UserSettings{.language = std::nullopt,
                                      .textFont = std::nullopt,
                                      .uiSkin = "modern",
                                      .minimap = "on"});
  CHECK(loadUserSettings(file).uiSkin == std::optional<std::string>{"modern"});
  CHECK(loadUserSettings(file).minimap == std::optional<std::string>{"on"});
}

} // namespace dwe::app
