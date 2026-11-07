#include "display_settings.hpp"

#include <dwe/core/log.hpp>

#include <utility>
#include <variant>

#include "messages.hpp"

namespace dwe::app {

DisplayStart startDisplay(bool fullscreenFlag, bool vsyncFlag, const UserSettings& settings) {
  DisplayStart start{
      .fullscreen = fullscreenFlag || settings.fullscreen.value_or(false),
      .vsync = vsyncFlag && settings.vsync.value_or(true),
      .uiScale = settings.uiScale.value_or(1.0F),
      .minimapSize = MinimapSizeMedium,
  };
  if (settings.minimapSize) {
    start.minimapSize = minimapSizeOf(ui::UiValue{*settings.minimapSize});
    if (start.minimapSize != *settings.minimapSize) {
      core::logWarning(messages::UnknownSavedMinimapSize, *settings.minimapSize);
    }
  }
  return start;
}

std::string_view minimapSizeOf(const ui::UiValue& value) {
  if (const std::string* size = std::get_if<std::string>(&value)) {
    for (const std::string_view known : {MinimapSizeSmall, MinimapSizeMedium, MinimapSizeLarge}) {
      if (*size == known) {
        return known;
      }
    }
  }
  return MinimapSizeMedium;
}

DisplayChanger::DisplayChanger(UserSettings& settings,
                               std::optional<std::filesystem::path> settingsFile)
    : settings_{&settings}, settingsFile_{std::move(settingsFile)} {}

void DisplayChanger::keepFullscreen(bool fullscreen) {
  settings_->fullscreen = fullscreen;
  save();
}

void DisplayChanger::keepVsync(bool vsync) {
  settings_->vsync = vsync;
  save();
}

void DisplayChanger::keepUiScale(float scale) {
  settings_->uiScale = scale;
  save();
}

std::string_view DisplayChanger::changeMinimapSize(const ui::UiValue& value) {
  const std::string_view size = minimapSizeOf(value);
  settings_->minimapSize = std::string{size};
  save();
  return size;
}

void DisplayChanger::save() const {
  if (settingsFile_) {
    saveUserSettings(*settingsFile_, *settings_);
  }
}

} // namespace dwe::app
