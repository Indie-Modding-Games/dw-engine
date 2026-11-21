#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/modes/game_mode.hpp>
#include <dwe/game/modes/mode_catalog.hpp>

#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mode_options.hpp"
#include "new_game_mode.hpp"
#include "user_settings.hpp"

using dwe::app::ModeOptions;
using dwe::app::NewGameModes;
using dwe::app::pinnedUiValue;
using dwe::app::PlayStart;
using dwe::app::UserSettings;
using dwe::game::modes::GameMode;
using dwe::game::modes::ModeCatalog;

namespace {

ModeCatalog shippedModes() {
  dwe::assets::MemoryFileSystem memory;
  memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
  dwe::assets::DiskFileSystem disk;
  const std::array packs{dwe::assets::PackLocation{.fileSystem = &memory,
                                                   .root = "base",
                                                   .fillsGaps = false,
                                                   .origin = dwe::assets::PackOrigin::Engine},
                         dwe::assets::PackLocation{.fileSystem = &disk,
                                                   .root = DWE_GAME_PACK,
                                                   .fillsGaps = false,
                                                   .origin = dwe::assets::PackOrigin::Engine}};
  auto files = dwe::assets::VirtualFileSystem::mount(packs);
  REQUIRE(files.has_value());
  return ModeCatalog::load(*files);
}

UserSettings everythingOn() {
  UserSettings settings;
  settings.difficulty = "easy";
  settings.rareItems = true;
  settings.freeExploration = true;
  settings.extraToilets = true;
  settings.chooseEggAfterLivesLost = true;
  settings.fairSicknessChance = true;
  settings.battleSpeed = "fast";
  settings.smoothMotion = false;
  return settings;
}

} // namespace

TEST_CASE("Classic Enhanced fixes the options that change the rules, and keeps the player's own") {
  const ModeCatalog modes = shippedModes();
  const GameMode* classic = modes.find(dwe::game::modes::ClassicMode);
  REQUIRE(classic != nullptr);
  ModeOptions fixed{classic, nullptr};
  CHECK(fixed.pins().size() == 5);
  const UserSettings player = everythingOn();
  const UserSettings played = fixed.over(player);
  CHECK(played.difficulty == "normal");
  CHECK(played.rareItems == false);
  CHECK(played.freeExploration == false);
  CHECK(played.extraToilets == false);
  CHECK(played.chooseEggAfterLivesLost == false);
  CHECK(played.fairSicknessChance == true);
  CHECK(played.battleSpeed == "fast");
  CHECK(played.smoothMotion == false);
  CHECK(player.difficulty == "easy");
  CHECK(player.rareItems == true);
}

TEST_CASE("Folder Mode fixes nothing: the game applies the player's settings as they are") {
  const ModeCatalog modes = shippedModes();
  ModeOptions folder{modes.find(dwe::game::modes::FolderMode), nullptr};
  CHECK(folder.pins().empty());
  CHECK_FALSE(folder.fixes("difficulty"));
  const UserSettings played = folder.over(everythingOn());
  CHECK(played.difficulty == "easy");
  CHECK(played.rareItems == true);
  ModeOptions none{nullptr, nullptr};
  CHECK(none.over(everythingOn()).freeExploration == true);
}

TEST_CASE("a change of a fixed option is refused; any other goes through") {
  const ModeCatalog modes = shippedModes();
  ModeOptions fixed{modes.find(dwe::game::modes::ClassicMode), nullptr};
  CHECK(fixed.fixes("rare_items"));
  CHECK(fixed.refuse("rare_items"));
  CHECK(fixed.refuse("difficulty"));
  CHECK_FALSE(fixed.refuse("fair_sickness_chance"));
  CHECK_FALSE(fixed.refuse("battle_speed"));
}

TEST_CASE("a pin the player's settings cannot take is left out, the others stay") {
  GameMode mode{.id = "odd",
                .nameKey = "a",
                .descriptionKey = "b",
                .ruleset = "original",
                .rulesets = {"original"},
                .order = 5,
                .options = {{.option = "rare_items", .value = "maybe"},
                            {.option = "extra_toilets", .value = "false"}}};
  ModeOptions fixed{&mode, nullptr};
  CHECK_FALSE(fixed.fixes("rare_items"));
  CHECK(fixed.fixes("extra_toilets"));
  CHECK(fixed.over(everythingOn()).rareItems == true);
  CHECK(fixed.over(everythingOn()).extraToilets == false);
}

TEST_CASE("a mode fixes only the options a game applies from its settings") {
  GameMode mode{.id = "odd",
                .nameKey = "a",
                .descriptionKey = "b",
                .ruleset = "original",
                .rulesets = {"original"},
                .order = 5,
                .options = {{.option = "smooth_motion", .value = "false"},
                            {.option = "difficulty", .value = "normal"}}};
  ModeOptions fixed{&mode, nullptr};
  CHECK_FALSE(fixed.fixes("smooth_motion"));
  CHECK(fixed.fixes("difficulty"));
}

TEST_CASE("a fixed value is published as the option's own kind") {
  CHECK(std::get<bool>(pinnedUiValue("true")));
  CHECK_FALSE(std::get<bool>(pinnedUiValue("false")));
  CHECK(std::get<double>(pinnedUiValue("3")) == 3.0);
  CHECK(std::get<std::string>(pinnedUiValue("normal")) == "normal");
  CHECK(std::get<std::string>(pinnedUiValue("3x")) == "3x");
}

TEST_CASE("the new game asks Classic Enhanced or Folder Mode; only Folder Mode asks the rules") {
  const NewGameModes modes{shippedModes()};
  CHECK(modes.asks());
  PlayStart classic;
  CHECK(modes.choose("classic", classic) == NewGameModes::Next::Start);
  CHECK(classic.modeId == "classic");
  CHECK(classic.rulesetId == "original");
  PlayStart folder;
  CHECK(modes.choose("folder", folder) == NewGameModes::Next::AskRules);
  CHECK(folder.modeId == "folder");
  CHECK_FALSE(folder.rulesetId.has_value());
}
