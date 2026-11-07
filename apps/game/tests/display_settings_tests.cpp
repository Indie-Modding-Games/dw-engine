// The options that set up the screen (display_settings.hpp): the window mode, vsync, the UI's
// scale and the minimap's size are saved like the other choices and the next run starts with them.

#include <dwe/test_support/temp_folder.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "display_settings.hpp"
#include "pause_menu_ui.hpp"
#include "user_settings.hpp"

namespace dwe::app {

TEST_CASE("the display choices survive a write and a read of the settings") {
  UserSettings settings;
  settings.fullscreen = true;
  settings.vsync = false;
  settings.uiScale = 1.15F;
  settings.minimapSize = "large";
  const auto text = formatUserSettings(settings);
  REQUIRE(text.has_value());
  const auto read = parseUserSettings(*text);
  REQUIRE_MESSAGE(read.has_value(), read.error().message);
  CHECK(read->fullscreen == std::optional<bool>{true});
  CHECK(read->vsync == std::optional<bool>{false});
  REQUIRE(read->uiScale.has_value());
  CHECK(*read->uiScale == doctest::Approx(1.15F));
  CHECK(read->minimapSize == std::optional<std::string>{"large"});
  // None chosen: none written, so a newer default still applies.
  const auto empty = formatUserSettings(UserSettings{});
  REQUIRE(empty.has_value());
  CHECK_FALSE(empty->contains("ui_scale"));
  CHECK_FALSE(empty->contains("fullscreen"));
}

TEST_CASE("the run starts with the saved display choices, the command line's flags winning") {
  const DisplayStart defaults = startDisplay(false, true, UserSettings{});
  CHECK_FALSE(defaults.fullscreen);
  CHECK(defaults.vsync);
  CHECK(defaults.uiScale == 1.0F);
  CHECK(defaults.minimapSize == MinimapSizeMedium);

  UserSettings saved;
  saved.fullscreen = true;
  saved.vsync = false;
  saved.uiScale = 1.5F;
  saved.minimapSize = "small";
  const DisplayStart start = startDisplay(false, true, saved);
  CHECK(start.fullscreen);
  CHECK_FALSE(start.vsync);
  CHECK(start.uiScale == 1.5F);
  CHECK(start.minimapSize == MinimapSizeSmall);

  // --fullscreen and --no-vsync win for their run over a saved window and vsync.
  UserSettings windowed;
  windowed.fullscreen = false;
  windowed.vsync = true;
  const DisplayStart flags = startDisplay(true, false, windowed);
  CHECK(flags.fullscreen);
  CHECK_FALSE(flags.vsync);

  // A saved size that is not one starts on the medium one.
  UserSettings unknown;
  unknown.minimapSize = "huge";
  CHECK(startDisplay(false, true, unknown).minimapSize == MinimapSizeMedium);
}

TEST_CASE("a saved UI scale outside the UI's range starts clamped to it") {
  test::MenuUi menu;
  UserSettings saved;
  saved.uiScale = 9.0F;
  menu.ui->setScale(startDisplay(false, true, saved).uiScale);
  CHECK(menu.ui->scale() == ui::Ui::MaxScale);
  saved.uiScale = 0.1F;
  menu.ui->setScale(startDisplay(false, true, saved).uiScale);
  CHECK(menu.ui->scale() == ui::Ui::MinScale);
}

TEST_CASE("the options page's UI scale row changes the scale and the next run starts with it") {
  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  UserSettings settings;
  settings.language = "es";
  DisplayChanger changer{settings, file};
  test::MenuUi menu;
  menu.ui->setSetting(UiScaleSetting, 1.0);
  menu.ui->setSetting("pause_menu.start_page", std::string{"options"});
  menu.ui->show("pause_menu", true);
  REQUIRE(menu.ui->callScript("pause_menu", "show_tab", std::vector<std::string>{"", "interface"}));
  menu.ui->update();
  std::ignore = menu.ui->takeCommands();
  // Each step goes to the game as in a run (game.cpp: apply): the UI takes the scale, the changer
  // keeps the one the UI took and the game publishes it back.
  const auto press = [&](const char* step) {
    REQUIRE(menu.ui->callScript("pause_menu", "adjust",
                                std::vector<std::string>{"", "ui-scale", step}));
    for (const ui::UiCommand& command : menu.ui->takeCommands()) {
      if (command.name == UiScaleSetting) {
        REQUIRE(std::holds_alternative<double>(command.value));
        menu.ui->setScale(static_cast<float>(std::get<double>(command.value)));
        changer.keepUiScale(menu.ui->scale());
        menu.ui->setSetting(UiScaleSetting, static_cast<double>(menu.ui->scale()));
      }
    }
    menu.ui->update();
  };
  press("1");
  CHECK(menu.ui->scale() == doctest::Approx(1.15F));
  const UserSettings read = loadUserSettings(file);
  REQUIRE(read.uiScale.has_value());
  CHECK(*read.uiScale == doctest::Approx(1.15F));
  // The other choices stay as they were.
  CHECK(read.language == std::optional<std::string>{"es"});
  CHECK(startDisplay(false, true, read).uiScale == doctest::Approx(1.15F));
  CHECK(menu.ui->elementText("pause_menu", "ui-scale-value").contains("115"));
  press("-1");
  press("-1");
  CHECK(*loadUserSettings(file).uiScale == doctest::Approx(0.9F));
}

TEST_CASE("the window mode, vsync and the minimap size are saved as they change") {
  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  UserSettings settings;
  DisplayChanger changer{settings, file};
  changer.keepFullscreen(true);
  changer.keepVsync(false);
  CHECK(changer.changeMinimapSize(ui::UiValue{std::string{"large"}}) == MinimapSizeLarge);
  const UserSettings read = loadUserSettings(file);
  CHECK(read.fullscreen == std::optional<bool>{true});
  CHECK(read.vsync == std::optional<bool>{false});
  CHECK(read.minimapSize == std::optional<std::string>{"large"});
  // A size that is not one is taken as the medium one.
  CHECK(changer.changeMinimapSize(ui::UiValue{true}) == MinimapSizeMedium);
  CHECK(loadUserSettings(file).minimapSize == std::optional<std::string>{"medium"});

  // A run that must not touch the user's settings changes nothing on disk.
  UserSettings unsaved;
  DisplayChanger autotest{unsaved, std::nullopt};
  autotest.keepFullscreen(false);
  autotest.keepUiScale(2.0F);
  CHECK(loadUserSettings(file).fullscreen == std::optional<bool>{true});
  CHECK_FALSE(loadUserSettings(file).uiScale.has_value());
}

} // namespace dwe::app
