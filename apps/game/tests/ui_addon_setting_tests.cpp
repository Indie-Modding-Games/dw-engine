// The player's UI addon (ui_addon_setting.hpp, docs/modding/interfaces.md): the start pick, safe
// mode, the commands an addon may send and the game's own UI back, with a notice, when the pick
// cannot go on.
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

} // namespace dwe::app
