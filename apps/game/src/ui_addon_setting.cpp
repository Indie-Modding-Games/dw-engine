#include "ui_addon_setting.hpp"

#include <dwe/core/error.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <set>
#include <utility>
#include <variant>

#include "messages.hpp"
#include "story_commands.hpp"
#include "ui_contract.hpp"

namespace dwe::app {

std::string startUiAddon(bool safe, const std::optional<std::string>& saved) {
  if (safe || !saved || saved->empty()) {
    return std::string{ui::DefaultUiId};
  }
  return *saved;
}

namespace {

// The battle's commands (battle_screen.hpp) and the window each one works in.
enum class BattleWindow : std::uint8_t { None, Fight, Bag, End };

BattleWindow battleWindowOf(std::string_view command) {
  if (command == "battle_command" || command == "battle_charge" || command == "battle_bag_open") {
    return BattleWindow::Fight;
  }
  if (command == "battle_bag_close") {
    return BattleWindow::Bag;
  }
  if (command == "battle_close" || command == "battle_end_count" ||
      command == "battle_end_counted") {
    return BattleWindow::End;
  }
  return BattleWindow::None;
}

} // namespace

BattleWindows battleWindowsOf(const ui::Ui& ui) {
  if (ui.setting("battle.active") != ui::UiValue{true}) {
    return {};
  }
  const ui::UiValue phase = ui.setting("battle.phase");
  const bool fight = phase == ui::UiValue{std::string{"fight"}};
  return {.fight = fight,
          .bag = fight && ui.setting("battle.bag.open") == ui::UiValue{true},
          .end = phase == ui::UiValue{std::string{"end"}}};
}

bool addonMaySend(std::string_view command, BattleWindows battle) {
  switch (battleWindowOf(command)) {
  case BattleWindow::Fight:
    if (!battle.fight) {
      return false;
    }
    break;
  case BattleWindow::Bag:
    if (!battle.bag) {
      return false;
    }
    break;
  case BattleWindow::End:
    if (!battle.end) {
      return false;
    }
    break;
  case BattleWindow::None:
    break;
  }
  static const std::set<std::string, std::less<>> declared = [] {
    std::set<std::string, std::less<>> names;
    for (const ui::UiContractEntry& entry : gameUiContract()) {
      if (entry.kind == ui::UiContractKind::Command || entry.kind == ui::UiContractKind::Option) {
        names.emplace(entry.name);
      }
    }
    return names;
  }();
  return declared.contains(command);
}

std::vector<ui::FoundUiAddon>
withLeftOutArchives(std::vector<ui::FoundUiAddon> found,
                    std::span<const content::LeftOutUiAddonArchive> archives) {
  for (const content::LeftOutUiAddonArchive& archive : archives) {
    if (!archive.shadowed) {
      found.push_back(
          ui::FoundUiAddon{.folder = archive.folder, .addon = core::fail(archive.reason)});
    }
  }
  return found;
}

UiAddonChanger::UiAddonChanger(ui::Ui& ui, UserSettings& settings,
                               std::optional<std::filesystem::path> settingsFile,
                               std::vector<ui::FoundUiAddon> found, bool safe)
    : ui_{&ui}, settings_{&settings}, settingsFile_{std::move(settingsFile)}, safe_{safe} {
  offered_.emplace_back(ui::DefaultUiId);
  for (ui::FoundUiAddon& addon : found) {
    if (!addon.addon) {
      core::logWarning(messages::UnreadableUiAddon, addon.folder, addon.addon.error().message);
      const std::size_t slash = addon.folder.rfind('/');
      unreadable_.insert_or_assign(addon.folder.substr(slash == std::string::npos ? 0 : slash + 1),
                                   addon.addon.error().message);
      continue;
    }
    if (addon.addon->type == ui::UiAddonType::Plugin) {
      plugins_.push_back(std::move(*addon.addon));
      continue;
    }
    offered_.push_back(addon.addon->id);
    addons_.push_back(std::move(*addon.addon));
  }
  // The battle's commands only in their windows; the story's, the naming box's and the dialog's
  // only while their box waits for them.
  ui_->setAddonCommandFilter([ui = ui_](std::string_view command) {
    return addonMaySend(command, battleWindowsOf(*ui)) &&
           storyCommandInWindow(command, storyWindowsOf(*ui));
  });
}

const ui::UiAddon* UiAddonChanger::find(std::string_view id) const {
  const auto found = std::ranges::find(addons_, id, &ui::UiAddon::id);
  return found == addons_.end() ? nullptr : &*found;
}

void UiAddonChanger::start() {
  if (safe_) {
    core::logInfo("{}", messages::SafeUiOn);
  }
  problem_.clear();
  useUi(startUiAddon(safe_, settings_->uiAddon));
  usePlugins(!safe_ && settings_->uiPlugins.value_or(true));
  finish();
}

bool UiAddonChanger::handle(const ui::UiCommand& command) {
  if (command.name == UiAddonReloadCommand) {
    pending_ = ui_->uiAddon().empty() ? std::string{ui::DefaultUiId} : std::string{ui_->uiAddon()};
    pendingPlugins_ = pluginsOn_;
    return true;
  }
  if (command.name == UiPluginsSetting && std::holds_alternative<bool>(command.value)) {
    if (safe_) {
      core::logWarning("{}", messages::SafeUiNoPlugins);
      return true;
    }
    settings_->uiPlugins = std::get<bool>(command.value);
    if (settingsFile_) {
      saveUserSettings(*settingsFile_, *settings_);
    }
    ui_->setSetting(UiPluginsSetting, *settings_->uiPlugins);
    pendingPlugins_ = *settings_->uiPlugins;
    return true;
  }
  if (command.name != UiAddonSetting || !std::holds_alternative<std::string>(command.value)) {
    return false;
  }
  const std::string& id = std::get<std::string>(command.value);
  if (safe_ || std::ranges::find(offered_, id) == offered_.end()) {
    core::logWarning(messages::UnknownUiAddon, id);
    return true;
  }
  settings_->uiAddon = id;
  if (settingsFile_) {
    saveUserSettings(*settingsFile_, *settings_);
  }
  // The row shows the pick at once; it goes on as the menu closes.
  ui_->setSetting(UiAddonSetting, id);
  pending_ = id;
  return true;
}

void UiAddonChanger::update(double seconds) {
  if ((pending_ || pendingPlugins_) && !ui_->capturesInput()) {
    problem_.clear();
    if (pending_) {
      useUi(*std::exchange(pending_, std::nullopt));
    }
    if (pendingPlugins_) {
      usePlugins(*std::exchange(pendingPlugins_, std::nullopt));
    }
    finish();
  }
  if (noticeLeft_ > 0.0) {
    noticeLeft_ -= seconds;
    if (noticeLeft_ <= 0.0) {
      ui_->hide(UiNoticeDocument);
    }
  }
}

void UiAddonChanger::addProblem(std::string problem) {
  problem_ += (problem_.empty() ? "" : "; ") + std::move(problem);
}

void UiAddonChanger::useUi(const std::string& id) {
  std::string problem;
  if (id == ui::DefaultUiId) {
    ui_->useDefaultUi();
  } else if (const ui::UiAddon* addon = find(id); addon == nullptr) {
    const auto unreadable = unreadable_.find(id);
    problem = unreadable != unreadable_.end() ? unreadable->second
                                              : core::formatMessage(messages::MissingUiAddon, id);
  } else {
    // Put on again from its files: a reload reads what changed.
    ui_->useDefaultUi();
    if (auto used = ui_->useUiAddon(*addon); !used) {
      problem = used.error().message;
    }
  }
  if (!problem.empty()) {
    core::logWarning(messages::UiAddonFellBack, id, problem);
    ui_->useDefaultUi();
    addProblem(std::move(problem));
  }
}

void UiAddonChanger::usePlugins(bool on) {
  // Off and on again: a reload reads what changed.
  ui_->removeUiPlugins();
  pluginsOn_ = on;
  if (!on) {
    return;
  }
  for (const ui::UiAddon& plugin : plugins_) {
    if (auto added = ui_->addUiPlugin(plugin); !added) {
      core::logWarning(messages::UiPluginLeftOut, plugin.id, added.error().message);
      addProblem(added.error().message);
    }
  }
}

void UiAddonChanger::finish() {
  publish();
  if (!problem_.empty()) {
    noticeLeft_ = UiNoticeSeconds;
    ui_->hide(UiNoticeDocument);
    ui_->show(UiNoticeDocument);
  }
}

void UiAddonChanger::publish() {
  std::string offered;
  for (const std::string& id : offered_) {
    offered += (offered.empty() ? "" : " ") + id;
  }
  ui_->setSetting(UiAddonsSetting, offered);
  for (const ui::UiAddon& addon : addons_) {
    ui_->setSetting(std::string{UiAddonNamePrefix} + addon.id, addon.name);
  }
  ui_->setSetting(UiAddonSafeSetting, safe_);
  ui_->setSetting(UiAddonProblemSetting, problem_);
  ui_->setSetting(UiPluginsSetting, pluginsOn_);
  ui_->setSetting(UiPluginsCountSetting, static_cast<double>(plugins_.size()));
  ui_->setSetting(UiAddonSetting, ui_->uiAddon().empty() ? std::string{ui::DefaultUiId}
                                                         : std::string{ui_->uiAddon()});
}

} // namespace dwe::app
