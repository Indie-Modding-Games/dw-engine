#include "minimap_setting.hpp"

#include <dwe/core/log.hpp>

#include <utility>
#include <variant>

#include "messages.hpp"

namespace dwe::app {

std::string_view minimapModeOf(const ui::UiValue& value) {
    if (const bool* visible = std::get_if<bool>(&value)) {
        return *visible ? MinimapOn : MinimapOff;
    }
    if (const std::string* mode = std::get_if<std::string>(&value)) {
        if (*mode == MinimapOn) {
            return MinimapOn;
        }
        if (*mode == MinimapOff) {
            return MinimapOff;
        }
    }
    return MinimapAuto;
}

std::string_view startMinimapMode(const std::optional<std::string>& saved) {
    if (!saved) {
        return MinimapAuto;
    }
    const std::string_view mode = minimapModeOf(ui::UiValue{*saved});
    if (mode == MinimapAuto && *saved != MinimapAuto) {
        core::logWarning(messages::UnknownSavedMinimap, *saved);
    }
    return mode;
}

MinimapChanger::MinimapChanger(UserSettings& settings, std::optional<std::filesystem::path> settingsFile)
    : settings_{&settings}, settingsFile_{std::move(settingsFile)} {}

std::string_view MinimapChanger::change(const ui::UiValue& value) {
    const std::string_view mode = minimapModeOf(value);
    settings_->minimap = std::string{mode};
    if (settingsFile_) {
        saveUserSettings(*settingsFile_, *settings_);
    }
    return mode;
}

} // namespace dwe::app
