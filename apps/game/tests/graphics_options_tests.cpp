#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/render/scene_light.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "graphics_options.hpp"
#include "pause_menu_ui.hpp"
#include "save_indicator.hpp"
#include "user_settings.hpp"

using dwe::app::GraphicsChanger;
using dwe::app::GraphicsOptions;
using dwe::app::SceneryQuality;
using dwe::app::ShadowMode;
using dwe::app::startGraphicsOptions;
using dwe::app::TextureFilter;
using dwe::app::withGraphicsFlag;
using dwe::app::test::MenuUi;

namespace {

std::string readPackFile(std::string_view path) {
  std::ifstream stream{std::filesystem::path{DWE_GAME_PACK} / path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("the graphics options start on, with the sun's shadows, and read what was saved") {
  const GraphicsOptions defaults = startGraphicsOptions({});
  CHECK(defaults.shadows == ShadowMode::Sun);
  CHECK(defaults.enhancedLighting);
  CHECK(defaults.vividColors);
  dwe::app::UserSettings settings;
  settings.shadows = "round";
  settings.enhancedLighting = false;
  settings.vividColors = false;
  const GraphicsOptions saved = startGraphicsOptions(settings);
  CHECK(saved.shadows == ShadowMode::Round);
  CHECK_FALSE(saved.enhancedLighting);
  CHECK_FALSE(saved.vividColors);
  // A mode that is not one starts with the default.
  settings.shadows = "square";
  CHECK(startGraphicsOptions(settings).shadows == ShadowMode::Sun);
}

TEST_CASE("--graphics changes the options it names and refuses what it cannot read") {
  const auto flagged = withGraphicsFlag({}, "shadows=round,lighting=off");
  REQUIRE(flagged);
  CHECK(flagged->shadows == ShadowMode::Round);
  CHECK_FALSE(flagged->enhancedLighting);
  CHECK(flagged->vividColors);
  const auto off = withGraphicsFlag({}, "shadows=off,colors=off");
  REQUIRE(off);
  CHECK(off->shadows == ShadowMode::Off);
  CHECK_FALSE(off->vividColors);
  CHECK_FALSE(withGraphicsFlag({}, "shadows=square"));
  CHECK_FALSE(withGraphicsFlag({}, "lighting=maybe"));
  CHECK_FALSE(withGraphicsFlag({}, "bloom=on"));
  CHECK_FALSE(withGraphicsFlag({}, "shadows"));
}

TEST_CASE("the pause menu's graphics rows change the options at once and save them") {
  const dwe::test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  dwe::app::UserSettings settings;
  settings.language = "es";
  GraphicsChanger changer{settings, file, startGraphicsOptions(settings)};
  MenuUi menu;
  changer.publish(*menu.ui);
  menu.ui->setSetting("pause_menu.start_page", std::string{"options"});
  menu.ui->show("pause_menu", true);
  REQUIRE(menu.ui->callScript("pause_menu", "show_tab", std::vector<std::string>{"", "graphics"}));
  menu.ui->update();
  CHECK(menu.ui->elementText("pause_menu", "shadows-value") == "Del sol");
  std::ignore = menu.ui->takeCommands();
  // The three rows of the Graphics tab, as game.cpp routes their commands.
  for (const std::string_view row : {"shadows", "enhanced-lighting", "vivid-colors"}) {
    REQUIRE(menu.ui->callScript("pause_menu", "adjust",
                                std::vector<std::string>{"", std::string{row}, "1"}));
  }
  for (const dwe::ui::UiCommand& command : menu.ui->takeCommands()) {
    CHECK(changer.handle(command, *menu.ui));
  }
  CHECK(changer.options() == GraphicsOptions{.shadows = ShadowMode::Round,
                                             .enhancedLighting = false,
                                             .vividColors = false});
  menu.ui->update();
  CHECK(menu.ui->elementText("pause_menu", "shadows-value") == "Redondas");
  CHECK(menu.ui->elementText("pause_menu", "enhanced-lighting-value") == "No");
  const dwe::app::UserSettings read = dwe::app::loadUserSettings(file);
  CHECK(read.shadows == std::optional<std::string>{"round"});
  CHECK(read.enhancedLighting == std::optional<bool>{false});
  CHECK(read.vividColors == std::optional<bool>{false});
  CHECK(startGraphicsOptions(read) == changer.options());
  // Other commands are not its own; a value of the wrong kind changes nothing.
  CHECK_FALSE(changer.handle({.name = "minimap", .value = std::string{"off"}}, *menu.ui));
  CHECK(changer.handle({.name = "shadows", .value = true}, *menu.ui));
  CHECK(changer.options().shadows == ShadowMode::Round);
}

TEST_CASE("the sharpness options start on their best, read what was saved and round-trip") {
  const GraphicsOptions defaults = startGraphicsOptions({});
  CHECK(defaults.sceneryQuality == SceneryQuality::Upscaled);
  CHECK(defaults.antialiasing);
  CHECK(defaults.textureFilter == TextureFilter::Upscaled);

  const dwe::test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  dwe::app::UserSettings settings;
  GraphicsChanger changer{settings, file, startGraphicsOptions(settings)};
  MenuUi menu;
  // As the console's and the routes' commands arrive (game.cpp).
  CHECK(changer.handle({.name = "scenery_quality", .value = std::string{"smooth"}}, *menu.ui));
  CHECK(changer.handle({.name = "antialiasing", .value = false}, *menu.ui));
  CHECK(changer.handle({.name = "texture_filter", .value = std::string{"nearest"}}, *menu.ui));
  CHECK(changer.options().sceneryQuality == SceneryQuality::Smooth);
  CHECK_FALSE(changer.options().antialiasing);
  CHECK(changer.options().textureFilter == TextureFilter::Nearest);
  const dwe::app::UserSettings read = dwe::app::loadUserSettings(file);
  CHECK(read.sceneryQuality == std::optional<std::string>{"smooth"});
  CHECK(read.antialiasing == std::optional<bool>{false});
  CHECK(read.textureFilter == std::optional<std::string>{"nearest"});
  CHECK(startGraphicsOptions(read) == changer.options());
  // A value that is not one changes nothing; a saved one starts with the best.
  CHECK(changer.handle({.name = "texture_filter", .value = std::string{"blurry"}}, *menu.ui));
  CHECK(changer.options().textureFilter == TextureFilter::Nearest);
  settings.textureFilter = "blurry";
  settings.sceneryQuality = "huge";
  CHECK(startGraphicsOptions(settings).textureFilter == TextureFilter::Upscaled);
  CHECK(startGraphicsOptions(settings).sceneryQuality == SceneryQuality::Upscaled);

  const auto flagged = withGraphicsFlag({}, "scenery=smooth,aa=off,textures=smooth");
  REQUIRE(flagged);
  CHECK(flagged->sceneryQuality == SceneryQuality::Smooth);
  CHECK_FALSE(flagged->antialiasing);
  CHECK(flagged->textureFilter == TextureFilter::Smooth);
  CHECK_FALSE(withGraphicsFlag({}, "scenery=sharp"));
  CHECK_FALSE(withGraphicsFlag({}, "textures=on"));
}

TEST_CASE("the pause menu's autosave row turns the autosave off and on and saves the choice") {
  const dwe::test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  dwe::app::UserSettings settings;
  settings.language = "es";
  MenuUi menu;
  menu.ui->setSetting(dwe::app::AutosaveSetting, settings.autosave.value_or(true));
  menu.ui->setSetting("pause_menu.start_page", std::string{"options"});
  menu.ui->show("pause_menu", true);
  menu.ui->update();
  // On by default.
  CHECK(menu.ui->elementText("pause_menu", "autosave-value") == "Sí");
  std::ignore = menu.ui->takeCommands();
  // A row of the Game tab; each press goes to the game as play_session.cpp routes it.
  const auto press = [&] {
    REQUIRE(
        menu.ui->callScript("pause_menu", "adjust", std::vector<std::string>{"", "autosave", "1"}));
    for (const dwe::ui::UiCommand& command : menu.ui->takeCommands()) {
      REQUIRE(command.name == dwe::app::AutosaveSetting);
      CHECK(dwe::app::changeAutosave(command.value, *menu.ui, settings, file).has_value());
    }
    menu.ui->update();
  };
  press();
  CHECK(menu.ui->elementText("pause_menu", "autosave-value") == "No");
  CHECK(dwe::app::loadUserSettings(file).autosave == std::optional<bool>{false});
  press();
  CHECK(menu.ui->elementText("pause_menu", "autosave-value") == "Sí");
  CHECK(dwe::app::loadUserSettings(file).autosave == std::optional<bool>{true});
}

TEST_CASE("the game pack's light table reads, and lights the maps it was made for") {
  const auto table = dwe::render::parseMapLights(readPackFile("field/lights.yaml"));
  REQUIRE_MESSAGE(table.has_value(), table.error().message);
  for (const std::string_view map : {"twna01", "leom01", "frzl01"}) {
    CAPTURE(map);
    CHECK_FALSE(table->of(map).empty());
  }
  for (const auto& [map, lights] : table->maps) {
    for (const dwe::render::MapLight& light : lights) {
      CAPTURE(map);
      // Inside a background (at most 640 x 480 pixels).
      CHECK(light.pixel.x >= 0.0F);
      CHECK(light.pixel.x <= 640.0F);
      CHECK(light.pixel.y >= 0.0F);
      CHECK(light.pixel.y <= 480.0F);
      CHECK(light.intensity <= 1.0F);
    }
  }
}
