#include "hd_scenery_setting.hpp"

#include <utility>
#include <variant>

namespace dwe::app {

bool startHdScenery(const UserSettings& settings) {
  return settings.hdScenery.value_or(true);
}

HdSceneryChanger::HdSceneryChanger(UserSettings& settings,
                                   std::optional<std::filesystem::path> settingsFile)
    : settings_{&settings}, settingsFile_{std::move(settingsFile)} {}

bool HdSceneryChanger::change(const ui::UiValue& value) {
  if (const bool* on = std::get_if<bool>(&value); on != nullptr && *on != this->on()) {
    settings_->hdScenery = *on;
    if (settingsFile_) {
      saveUserSettings(*settingsFile_, *settings_);
    }
  }
  return on();
}

} // namespace dwe::app
