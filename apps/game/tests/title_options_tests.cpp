// The title screen's options (docs/modulos/apps/game.md, «Opciones en partida»): with no game
// running, the options page's on/off rows that only a game applies are kept in the user's settings
// and shown back, so the next game starts with them.

#include <dwe/test_support/temp_folder.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "pause_menu_ui.hpp"
#include "title_options.hpp"
#include "user_settings.hpp"

namespace dwe::app {
namespace {

// The page's row of a setting: its name with dashes.
std::string rowOf(std::string_view setting) {
  std::string row{setting};
  std::ranges::replace(row, '_', '-');
  return row;
}

} // namespace

TEST_CASE("the title publishes each game option's saved value, or its default") {
  test::MenuUi menu;
  UserSettings settings;
  publishGameToggles(*menu.ui, settings);
  for (const GameToggle& toggle : gameToggles()) {
    CAPTURE(toggle.setting);
    CHECK(menu.ui->setting(toggle.setting) == ui::UiValue{toggle.fallback});
  }
  settings.freeExploration = true;
  settings.worldMap = false;
  publishGameToggles(*menu.ui, settings);
  CHECK(menu.ui->setting("free_exploration") == ui::UiValue{true});
  CHECK(menu.ui->setting("world_map") == ui::UiValue{false});
}

TEST_CASE("every game option's row on the title's options page changes and saves it") {
  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  UserSettings settings;
  test::MenuUi menu;
  publishGameToggles(*menu.ui, settings);
  menu.ui->setSetting(MenuAtTitleSetting, true);
  menu.ui->setSetting("pause_menu.start_page", std::string{"options"});
  menu.ui->show("pause_menu", true);
  menu.ui->setSetting("pause_menu.start_page", std::string{});
  menu.ui->setSetting(MenuAtTitleSetting, false);
  menu.ui->update();
  std::ignore = menu.ui->takeCommands();

  std::vector<std::string_view> changed;
  for (const std::string_view tab : {"game", "graphics", "sound", "interface"}) {
    REQUIRE(menu.ui->callScript("pause_menu", "show_tab",
                                std::vector<std::string>{"", std::string{tab}}));
    menu.ui->update();
    for (const GameToggle& toggle : gameToggles()) {
      const std::string row = rowOf(toggle.setting);
      if (!menu.ui->elementShown("pause_menu", row)) {
        continue;
      }
      CAPTURE(toggle.setting);
      const std::string before = menu.ui->elementText("pause_menu", row + "-value");
      REQUIRE(menu.ui->callScript("pause_menu", "adjust", std::vector<std::string>{"", row, "1"}));
      for (const ui::UiCommand& command : menu.ui->takeCommands()) {
        CHECK(command.name == toggle.setting);
        CHECK(changeGameToggle(command, *menu.ui, settings, file));
      }
      menu.ui->update();
      CHECK(menu.ui->elementText("pause_menu", row + "-value") != before);
      changed.push_back(toggle.setting);
    }
  }
  // Each one is on one of the tabs, and each one is saved flipped.
  CHECK(changed.size() == gameToggles().size());
  const UserSettings saved = loadUserSettings(file);
  for (const GameToggle& toggle : gameToggles()) {
    CAPTURE(toggle.setting);
    CHECK(saved.*toggle.saved == std::optional<bool>{!toggle.fallback});
  }
}

TEST_CASE("a game option's command keeps a true or false and leaves other commands alone") {
  test::MenuUi menu;
  UserSettings settings;
  CHECK(changeGameToggle(ui::UiCommand{.name = "world_map", .value = false}, *menu.ui, settings,
                         std::nullopt));
  CHECK(settings.worldMap == std::optional<bool>{false});
  CHECK(menu.ui->setting("world_map") == ui::UiValue{false});
  // Not a switch's value: taken, nothing changes.
  CHECK(changeGameToggle(ui::UiCommand{.name = "world_map", .value = std::string{"on"}}, *menu.ui,
                         settings, std::nullopt));
  CHECK(settings.worldMap == std::optional<bool>{false});
  CHECK_FALSE(changeGameToggle(ui::UiCommand{.name = "shadows", .value = 3.0}, *menu.ui, settings,
                               std::nullopt));
  CHECK_FALSE(changeGameToggle(ui::UiCommand{.name = "quit", .value = true}, *menu.ui, settings,
                               std::nullopt));
}

} // namespace dwe::app
