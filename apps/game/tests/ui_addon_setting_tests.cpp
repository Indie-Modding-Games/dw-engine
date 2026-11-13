// The player's UI addon (ui_addon_setting.hpp, docs/modding/interfaces.md): the start pick, safe
// mode, the commands an addon may send and the game's own UI back, with a notice, when the pick
// cannot go on.
#include <dwe/content/user_ui_addons.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ui/ui.hpp>
#include <dwe/ui/ui_addon.hpp>
#include <dwe/ui/ui_contract.hpp>

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "pause_menu_ui.hpp"
#include "ui_addon_setting.hpp"
#include "user_settings.hpp"

namespace dwe::app {
namespace {

// An addon whose document the packs do not have, and one whose manifest does not read.
std::vector<ui::FoundUiAddon> brokenAddons() {
  ui::UiAddon broken;
  broken.folder = "ui/addons/broken";
  broken.id = "broken";
  broken.name = "Broken";
  broken.version = "1.0.0";
  broken.uiApi =
      ui::UiApiRange{.minimum = ui::CurrentUiApiVersion,
                     .below = ui::UiApiVersion{.major = ui::CurrentUiApiVersion.major + 1}};
  broken.screens.emplace("pause_menu", *assets::AssetId::parse("ui/addons/broken/menu/document"));
  std::vector<ui::FoundUiAddon> found;
  found.push_back(ui::FoundUiAddon{.folder = "ui/addons/broken", .addon = std::move(broken)});
  found.push_back(
      ui::FoundUiAddon{.folder = "ui/addons/bad", .addon = core::fail("the manifest is bad")});
  return found;
}

// A plugin whose document the packs do not have.
ui::FoundUiAddon brokenPlugin() {
  ui::UiAddon plugin;
  plugin.folder = "ui/addons/ticker";
  plugin.id = "ticker";
  plugin.name = "Ticker";
  plugin.version = "1.0.0";
  plugin.type = ui::UiAddonType::Plugin;
  plugin.uiApi =
      ui::UiApiRange{.minimum = ui::CurrentUiApiVersion,
                     .below = ui::UiApiVersion{.major = ui::CurrentUiApiVersion.major + 1}};
  plugin.documents.push_back(*assets::AssetId::parse("ui/addons/ticker/document"));
  return ui::FoundUiAddon{.folder = "ui/addons/ticker", .addon = std::move(plugin)};
}

void loadNotice(ui::Ui& ui) {
  auto notice = assets::AssetId::parse("ui/ui_notice/document");
  REQUIRE(notice.has_value());
  REQUIRE(ui.loadDocument(UiNoticeDocument, *notice));
}

} // namespace

TEST_CASE("the run starts with the saved UI addon, or the game's own in safe mode") {
  CHECK(startUiAddon(false, std::nullopt) == ui::DefaultUiId);
  CHECK(startUiAddon(false, std::string{"clock"}) == "clock");
  CHECK(startUiAddon(true, std::string{"clock"}) == ui::DefaultUiId);
}

TEST_CASE("an addon may send only the commands and options of the UI contract") {
  CHECK(addonMaySend("ui_skin"));
  CHECK(addonMaySend(UiAddonSetting));
  CHECK(addonMaySend(UiAddonReloadCommand));
  CHECK_FALSE(addonMaySend("ui_addon.problem"));
  CHECK_FALSE(addonMaySend("not_a_command"));
}

TEST_CASE("an addon may send a battle's commands only in the window the battle takes them") {
  // Out of a battle none goes through.
  for (const std::string_view command :
       {"battle_command", "battle_charge", "battle_bag_open", "battle_bag_close", "battle_close",
        "battle_end_count", "battle_end_counted"}) {
    CAPTURE(command);
    CHECK_FALSE(addonMaySend(command));
  }
  const BattleWindows fight{.fight = true};
  CHECK(addonMaySend("battle_command", fight));
  CHECK(addonMaySend("battle_charge", fight));
  CHECK(addonMaySend("battle_bag_open", fight));
  CHECK_FALSE(addonMaySend("battle_bag_close", fight));
  CHECK_FALSE(addonMaySend("battle_close", fight));
  const BattleWindows bag{.fight = true, .bag = true};
  CHECK(addonMaySend("battle_bag_close", bag));
  const BattleWindows end{.end = true};
  CHECK(addonMaySend("battle_close", end));
  CHECK(addonMaySend("battle_end_count", end));
  CHECK(addonMaySend("battle_end_counted", end));
  CHECK_FALSE(addonMaySend("battle_command", end));
  // The other commands do not depend on a battle.
  CHECK(addonMaySend("ui_skin", end));

  // The windows come from what the battle publishes.
  test::MenuUi menu;
  CHECK_FALSE(battleWindowsOf(*menu.ui).fight);
  menu.ui->setSetting("battle.active", true);
  menu.ui->setSetting("battle.phase", std::string{"intro"});
  CHECK_FALSE(battleWindowsOf(*menu.ui).fight);
  menu.ui->setSetting("battle.phase", std::string{"fight"});
  CHECK(battleWindowsOf(*menu.ui).fight);
  CHECK_FALSE(battleWindowsOf(*menu.ui).bag);
  menu.ui->setSetting("battle.bag.open", true);
  CHECK(battleWindowsOf(*menu.ui).bag);
  menu.ui->setSetting("battle.phase", std::string{"end"});
  CHECK(battleWindowsOf(*menu.ui).end);
  CHECK_FALSE(battleWindowsOf(*menu.ui).bag);
  menu.ui->setSetting("battle.active", false);
  CHECK_FALSE(battleWindowsOf(*menu.ui).end);
}

TEST_CASE("a UI addon that cannot go on gives the game's own UI back, with a notice") {
  test::MenuUi menu;
  loadNotice(*menu.ui);
  UserSettings settings;
  settings.uiAddon = "broken";
  UiAddonChanger changer{*menu.ui, settings, std::nullopt, brokenAddons(), false};
  // The manifest that does not read is not offered.
  REQUIRE(changer.offered().size() == 2);
  CHECK(changer.offered()[1] == "broken");
  changer.start();
  CHECK(menu.ui->uiAddon().empty());
  CHECK(menu.ui->setting(UiAddonSetting) == ui::UiValue{std::string{ui::DefaultUiId}});
  CHECK(menu.ui->setting(UiAddonsSetting) == ui::UiValue{std::string{"default broken"}});
  CHECK(menu.ui->setting("ui_addon.name.broken") == ui::UiValue{std::string{"Broken"}});
  const ui::UiValue problemValue = menu.ui->setting(UiAddonProblemSetting);
  const auto* problem = std::get_if<std::string>(&problemValue);
  REQUIRE(problem != nullptr);
  CHECK(problem->contains("broken"));
  CHECK(menu.ui->isVisible(UiNoticeDocument));
  changer.update(UiNoticeSeconds + 1.0);
  CHECK_FALSE(menu.ui->isVisible(UiNoticeDocument));
  // The player's pick is saved and goes on once no modal screen is shown.
  CHECK(changer.handle(
      ui::UiCommand{.name = std::string{UiAddonSetting}, .value = std::string{ui::DefaultUiId}}));
  CHECK(settings.uiAddon == std::optional<std::string>{ui::DefaultUiId});
  changer.update(0.0);
  CHECK(menu.ui->setting(UiAddonProblemSetting) == ui::UiValue{std::string{}});
  CHECK_FALSE(changer.handle(ui::UiCommand{.name = "ui_skin", .value = std::string{"modern"}}));
}

TEST_CASE("a saved UI addon whose .zip gives none brings the game's own UI back, saying why") {
  test::MenuUi menu;
  loadNotice(*menu.ui);
  UserSettings settings;
  settings.uiAddon = "clock";
  const std::vector<content::LeftOutUiAddonArchive> archives{
      content::LeftOutUiAddonArchive{.folder = "ui/addons/clock",
                                     .reason = "ui/addons/clock.zip: evil.lua is encrypted",
                                     .shadowed = false},
      // A folder of its id is used instead: no addon that does not read.
      content::LeftOutUiAddonArchive{.folder = "ui/addons/bad",
                                     .reason = "ui/addons/bad.zip: the folder has its id",
                                     .shadowed = true},
  };
  const std::vector<ui::FoundUiAddon> found = withLeftOutArchives(brokenAddons(), archives);
  REQUIRE(found.size() == 3);
  CHECK(found[2].folder == "ui/addons/clock");
  UiAddonChanger changer{*menu.ui, settings, std::nullopt, found, false};
  CHECK(changer.offered().size() == 2);
  changer.start();
  CHECK(menu.ui->uiAddon().empty());
  CHECK(menu.ui->setting(UiAddonProblemSetting) ==
        ui::UiValue{std::string{"ui/addons/clock.zip: evil.lua is encrypted"}});
  CHECK(menu.ui->isVisible(UiNoticeDocument));
}

TEST_CASE("safe mode keeps the game's own UI, whatever was saved or picked") {
  test::MenuUi menu;
  loadNotice(*menu.ui);
  UserSettings settings;
  settings.uiAddon = "broken";
  UiAddonChanger changer{*menu.ui, settings, std::nullopt, brokenAddons(), true};
  changer.start();
  CHECK(menu.ui->setting(UiAddonSafeSetting) == ui::UiValue{true});
  CHECK_FALSE(menu.ui->isVisible(UiNoticeDocument));
  CHECK(changer.handle(
      ui::UiCommand{.name = std::string{UiAddonSetting}, .value = std::string{"broken"}}));
  CHECK(settings.uiAddon == std::optional<std::string>{"broken"});
  changer.update(0.0);
  CHECK(menu.ui->uiAddon().empty());
  CHECK(menu.ui->setting(UiAddonSetting) == ui::UiValue{std::string{ui::DefaultUiId}});
}

TEST_CASE("the plugins go on with the game, all of them, and a broken one is left out with a "
          "notice") {
  test::MenuUi menu;
  loadNotice(*menu.ui);
  UserSettings settings;
  std::vector<ui::FoundUiAddon> found;
  found.push_back(brokenPlugin());
  UiAddonChanger changer{*menu.ui, settings, std::nullopt, std::move(found), false};
  // A plugin is no UI to pick.
  CHECK(changer.offered().size() == 1);
  REQUIRE(changer.plugins().size() == 1);
  changer.start();
  CHECK(menu.ui->setting(UiPluginsCountSetting) == ui::UiValue{1.0});
  CHECK(menu.ui->setting(UiPluginsSetting) == ui::UiValue{true});
  CHECK(menu.ui->uiPlugins().empty());
  CHECK(menu.ui->isVisible(UiNoticeDocument));
  // Turned off: saved, and no notice the next time.
  CHECK(changer.handle(ui::UiCommand{.name = std::string{UiPluginsSetting}, .value = false}));
  CHECK(settings.uiPlugins == std::optional<bool>{false});
  changer.update(0.0);
  CHECK(menu.ui->setting(UiPluginsSetting) == ui::UiValue{false});
  CHECK(menu.ui->setting(UiAddonProblemSetting) == ui::UiValue{std::string{}});
}

} // namespace dwe::app
