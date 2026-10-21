#include "ui_skin_setting.hpp"

#include <dwe/core/log.hpp>
#include <dwe/ui/skin.hpp>

#include <utility>

#include "messages.hpp"

namespace dwe::app {

std::optional<std::string> startUiSkin(const std::optional<std::string>& commandLine,
                                       const std::optional<std::string>& saved) {
    return commandLine ? commandLine : saved;
}

void applyStartUiSkin(ui::Ui& ui, const std::optional<std::string>& skin) {
    if (skin) {
        if (auto changed = ui.setSkin(*skin); !changed) {
            core::logWarning(messages::UnusableUiSkin, *skin, changed.error().message);
        }
    }
    ui.setSetting(ui::UiSkinSetting, std::string{ui.skin()});
}

UiSkinChanger::UiSkinChanger(ui::Ui& ui, UserSettings& settings, std::optional<std::filesystem::path> settingsFile)
    : ui_{&ui}, settings_{&settings}, settingsFile_{std::move(settingsFile)} {}

void UiSkinChanger::change(std::string_view id) {
    if (auto changed = ui_->setSkin(id); !changed) {
        core::logWarning(messages::UnusableUiSkin, id, changed.error().message);
        return;
    }
    ui_->setSetting(ui::UiSkinSetting, std::string{ui_->skin()});
    settings_->uiSkin = std::string{ui_->skin()};
    if (settingsFile_) {
        saveUserSettings(*settingsFile_, *settings_);
    }
}

} // namespace dwe::app
