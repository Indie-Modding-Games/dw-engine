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
#include "save_indicator.hpp"
#include "user_settings.hpp"

using dwe::app::GraphicsChanger;
using dwe::app::GraphicsOptions;
using dwe::app::ShadowMode;
using dwe::app::startGraphicsOptions;
using dwe::app::withGraphicsFlag;

namespace {

std::string readPackFile(std::string_view path) {
  std::ifstream stream{std::filesystem::path{DWE_GAME_PACK} / path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

// The game pack's pause menu at 1280x720, in Spanish.
struct MenuUi {
  dwe::assets::MemoryFileSystem memory;
  std::unique_ptr<dwe::assets::VirtualFileSystem> files;
  std::unique_ptr<dwe::localization::Catalog> texts;
  std::unique_ptr<dwe::ui::Ui> ui;

  MenuUi() {
    const std::filesystem::path pack{DWE_GAME_PACK};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{pack}) {
      if (entry.is_regular_file()) {
        std::ifstream stream{entry.path(), std::ios::binary};
        memory.addFile(
            std::filesystem::path{"packs/game"} / entry.path().lexically_relative(pack),
            std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}});
      }
    }
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    const auto location = [this](std::string_view root) {
      return dwe::assets::PackLocation{.fileSystem = &memory,
                                       .root = std::string{root},
                                       .origin = dwe::assets::PackOrigin::Engine};
    };
    const std::array locations{location("packs/base"), location("packs/game")};
    auto mounted = dwe::assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<dwe::assets::VirtualFileSystem>(std::move(*mounted));
    auto catalog = dwe::localization::Catalog::load(*files, "es");
    REQUIRE_MESSAGE(catalog.has_value(), catalog.error().message);
    texts = std::make_unique<dwe::localization::Catalog>(std::move(*catalog));
    auto created = dwe::ui::Ui::create(dwe::ui::UiDesc{.files = files.get(),
                                                       .texts = texts.get(),
                                                       .width = 1280,
                                                       .height = 720,
                                                       .clock = {},
                                                       .scripts = {}});
    REQUIRE_MESSAGE(created.has_value(), created.error().message);
    ui = std::move(*created);
    auto menu = dwe::assets::AssetId::parse("ui/pause_menu/document");
    REQUIRE_MESSAGE(menu.has_value(), menu.error().message);
    auto loaded = ui->loadDocument("pause_menu", *menu);
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
  }
};

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
  menu.ui->update();
  CHECK(menu.ui->elementText("pause_menu", "shadows-value") == "Del sol");
  std::ignore = menu.ui->takeCommands();
  // The three rows, as game.cpp routes their commands.
  for (const std::string_view slot : {"19", "20", "21"}) {
    REQUIRE(menu.ui->callScript("pause_menu", "adjust_slot",
                                std::vector<std::string>{"", std::string{slot}, "1"}));
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
  // The 25th of the game's options; each press goes to the game as play_session.cpp routes it.
  const auto press = [&] {
    REQUIRE(
        menu.ui->callScript("pause_menu", "adjust_slot", std::vector<std::string>{"", "25", "1"}));
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
