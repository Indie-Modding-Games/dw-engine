#include "ui_addon_setting.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <algorithm>
#include <ranges>
#include <set>
#include <utility>
#include <variant>

#include "messages.hpp"
#include "ui_contract.hpp"

namespace dwe::app {

std::string startUiAddon(bool safe, const std::optional<std::string>& saved) {
  if (safe || !saved || saved->empty()) {
    return std::string{ui::DefaultUiId};
  }
  return *saved;
}

bool addonMaySend(std::string_view command) {
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

UiAddonChanger::UiAddonChanger(ui::Ui& ui, UserSettings& settings,
                               std::optional<std::filesystem::path> settingsFile,
                               std::vector<ui::FoundUiAddon> found, bool safe)
    : ui_{&ui}, settings_{&settings}, settingsFile_{std::move(settingsFile)}, safe_{safe} {
  offered_.emplace_back(ui::DefaultUiId);
  for (ui::FoundUiAddon& addon : found) {
    if (!addon.addon) {
      core::logWarning(messages::UnreadableUiAddon, addon.folder, addon.addon.error().message);
      continue;
    }
    if (addon.addon->type != ui::UiAddonType::Ui) {
      continue;
    }
    offered_.push_back(addon.addon->id);
    addons_.push_back(std::move(*addon.addon));
  }
  ui_->setAddonCommandFilter(addonMaySend);
}

const ui::UiAddon* UiAddonChanger::find(std::string_view id) const {
  const auto found = std::ranges::find(addons_, id, &ui::UiAddon::id);
  return found == addons_.end() ? nullptr : &*found;
}

void UiAddonChanger::start() {
  if (safe_) {
    core::logInfo("{}", messages::SafeUiOn);
  }
  use(startUiAddon(safe_, settings_->uiAddon));
}

bool UiAddonChanger::handle(const ui::UiCommand& command) {
  if (command.name == UiAddonReloadCommand) {
    pending_ = ui_->uiAddon().empty() ? std::string{ui::DefaultUiId} : std::string{ui_->uiAddon()};
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
  if (pending_ && !ui_->capturesInput()) {
    use(*std::exchange(pending_, std::nullopt));
  }
  if (noticeLeft_ > 0.0) {
    noticeLeft_ -= seconds;
    if (noticeLeft_ <= 0.0) {
      ui_->hide(UiNoticeDocument);
    }
  }
}

void UiAddonChanger::use(const std::string& id) {
  problem_.clear();
  if (id == ui::DefaultUiId) {
    ui_->useDefaultUi();
  } else if (const ui::UiAddon* addon = find(id); addon == nullptr) {
    problem_ = core::formatMessage(messages::MissingUiAddon, id);
  } else {
    // Put on again from its files: a reload reads what changed.
    ui_->useDefaultUi();
    if (auto used = ui_->useUiAddon(*addon); !used) {
      problem_ = used.error().message;
    }
  }
  if (!problem_.empty()) {
    core::logWarning(messages::UiAddonFellBack, id, problem_);
    ui_->useDefaultUi();
    noticeLeft_ = UiNoticeSeconds;
  }
  publish();
  if (!problem_.empty()) {
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
  ui_->setSetting(UiAddonSetting, ui_->uiAddon().empty() ? std::string{ui::DefaultUiId}
                                                         : std::string{ui_->uiAddon()});
}

} // namespace dwe::app
