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
                              .uiAddon = "big",
                              .minimap = "off",
                              .freeExploration = true,
                              .smoothMotion = false,
                              .saveNotice = false,
                              .autosave = false,
                              .digitalTime = false,
                              .evolutionRequirements = false,
                              .translatedSigns = false,
                              .statusExtras = false,
                              .canonicalStages = true,
                              .rareItems = true,
                              .walkControl = "mouse",
                              .continuousMusic = false,
                              .needBubbleStack = false,
                              .worldMap = std::nullopt,
                              .chibiStyle = "all",
                              .minimapEnemies = false,
                              .minimapItems = false,
                              .shadows = "round",
                              .enhancedLighting = false,
                              .vividColors = false,
                              .musicVolume = 3,
                              .effectsVolume = 0,
                              .reverb = false,
                              .battleSpeed = "key",
                              .enemyHpBars = true,
                              .gymSessions = 4,
                              .quickBattleStart = true,
                              .difficulty = "easy",
                              .skipEvolution = true,
                              .choiceCancelCloses = true,
                              .fairSicknessChance = true,
                              .fairEvolutionAverage = false,
                              .fairMoveForgetting = true,
                              .fairCardStock = false,
                              .chooseEggAfterLivesLost = true,
                              .generousTalk = true,
                              .extraToilets = true};
  const auto text = formatUserSettings(settings);
  REQUIRE(text.has_value());
  CHECK(text->contains("ui_skin: test_contrast"));
  const auto read = parseUserSettings(*text);
  REQUIRE_MESSAGE(read.has_value(), read.error().message);
  CHECK(read->language == std::optional<std::string>{"es"});
  CHECK(read->textFont == std::optional<std::string>{"original"});
  CHECK(read->uiSkin == std::optional<std::string>{"test_contrast"});
  CHECK(read->uiAddon == std::optional<std::string>{"big"});
  CHECK(read->minimap == std::optional<std::string>{"off"});
  CHECK(read->freeExploration == std::optional<bool>{true});
  CHECK(read->smoothMotion == std::optional<bool>{false});
  CHECK(read->saveNotice == std::optional<bool>{false});
  CHECK(read->autosave == std::optional<bool>{false});
  CHECK(read->digitalTime == std::optional<bool>{false});
  CHECK(read->evolutionRequirements == std::optional<bool>{false});
  CHECK(read->translatedSigns == std::optional<bool>{false});
  CHECK(read->statusExtras == std::optional<bool>{false});
  CHECK(read->canonicalStages == std::optional<bool>{true});
  CHECK(read->rareItems == std::optional<bool>{true});
  CHECK(read->walkControl == std::optional<std::string>{"mouse"});
  CHECK(read->continuousMusic == std::optional<bool>{false});
  CHECK(read->needBubbleStack == std::optional<bool>{false});
  CHECK(read->chibiStyle == std::optional<std::string>{"all"});
  CHECK(read->minimapEnemies == std::optional<bool>{false});
  CHECK(read->minimapItems == std::optional<bool>{false});
  CHECK(read->shadows == std::optional<std::string>{"round"});
  CHECK(read->enhancedLighting == std::optional<bool>{false});
  CHECK(read->vividColors == std::optional<bool>{false});
  CHECK(read->musicVolume == std::optional<int>{3});
  CHECK(read->effectsVolume == std::optional<int>{0});
  CHECK(read->reverb == std::optional<bool>{false});
  CHECK(read->battleSpeed == std::optional<std::string>{"key"});
  CHECK(read->enemyHpBars == std::optional<bool>{true});
  CHECK(read->gymSessions == std::optional<int>{4});
  CHECK(read->quickBattleStart == std::optional<bool>{true});
  CHECK(read->difficulty == std::optional<std::string>{"easy"});
  CHECK(read->skipEvolution == std::optional<bool>{true});
  CHECK(read->choiceCancelCloses == std::optional<bool>{true});
  CHECK(read->fairSicknessChance == std::optional<bool>{true});
  CHECK(read->fairEvolutionAverage == std::optional<bool>{false});
  CHECK(read->fairMoveForgetting == std::optional<bool>{true});
  CHECK(read->fairCardStock == std::optional<bool>{false});
  CHECK(read->chooseEggAfterLivesLost == std::optional<bool>{true});
  CHECK(read->generousTalk == std::optional<bool>{true});
  CHECK(read->extraToilets == std::optional<bool>{true});

  // A file from before the setting existed, or without the choice made, leaves it to the packs.
  const auto older = parseUserSettings("language: en\n");
  REQUIRE(older.has_value());
  CHECK_FALSE(older->uiSkin.has_value());
  CHECK_FALSE(older->minimap.has_value());
  CHECK_FALSE(older->freeExploration.has_value());
  CHECK_FALSE(older->smoothMotion.has_value());
  CHECK_FALSE(older->saveNotice.has_value());
  CHECK_FALSE(older->autosave.has_value());
  CHECK_FALSE(older->digitalTime.has_value());
  CHECK_FALSE(older->evolutionRequirements.has_value());
  CHECK_FALSE(older->translatedSigns.has_value());
  CHECK_FALSE(older->statusExtras.has_value());
  CHECK_FALSE(older->canonicalStages.has_value());
  CHECK_FALSE(older->rareItems.has_value());
  CHECK_FALSE(older->walkControl.has_value());
  CHECK_FALSE(older->continuousMusic.has_value());
  CHECK_FALSE(older->needBubbleStack.has_value());
  CHECK_FALSE(older->chibiStyle.has_value());
  CHECK_FALSE(older->minimapEnemies.has_value());
  CHECK_FALSE(older->minimapItems.has_value());
  CHECK_FALSE(older->shadows.has_value());
  CHECK_FALSE(older->enhancedLighting.has_value());
  CHECK_FALSE(older->vividColors.has_value());
  CHECK_FALSE(older->musicVolume.has_value());
  CHECK_FALSE(older->effectsVolume.has_value());
  CHECK_FALSE(older->battleSpeed.has_value());
  CHECK_FALSE(older->enemyHpBars.has_value());
  CHECK_FALSE(older->gymSessions.has_value());
  CHECK_FALSE(older->quickBattleStart.has_value());
  CHECK_FALSE(older->difficulty.has_value());
  CHECK_FALSE(older->skipEvolution.has_value());
  CHECK_FALSE(older->choiceCancelCloses.has_value());
  CHECK_FALSE(older->fairSicknessChance.has_value());
  CHECK_FALSE(older->fairEvolutionAverage.has_value());
  CHECK_FALSE(older->fairMoveForgetting.has_value());
  CHECK_FALSE(older->generousTalk.has_value());
  CHECK_FALSE(older->extraToilets.has_value());
  CHECK_FALSE(older->fairCardStock.has_value());
  CHECK_FALSE(older->chooseEggAfterLivesLost.has_value());
  CHECK_FALSE(formatUserSettings(UserSettings{})->contains("ui_skin"));
}

TEST_CASE("the player's settings file is written and read back") {
  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  CHECK_FALSE(loadUserSettings(file).uiSkin.has_value());
  saveUserSettings(file, UserSettings{.language = std::nullopt,
                                      .textFont = std::nullopt,
                                      .uiSkin = "modern",
                                      .uiAddon = std::nullopt,
                                      .minimap = "on"});
  CHECK(loadUserSettings(file).uiSkin == std::optional<std::string>{"modern"});
  CHECK(loadUserSettings(file).minimap == std::optional<std::string>{"on"});
}

} // namespace dwe::app
