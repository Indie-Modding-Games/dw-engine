#include "walk_control.hpp"

#include <dwe/core/log.hpp>

#include <utility>
#include <variant>

#include "messages.hpp"

namespace dwe::app {

std::optional<WalkControl> parseWalkControl(std::string_view name) {
  if (name == WalkControlKeys) {
    return WalkControl::Keys;
  }
  if (name == WalkControlMouse) {
    return WalkControl::Mouse;
  }
  return std::nullopt;
}

std::string_view walkControlName(WalkControl control) {
  return control == WalkControl::Mouse ? WalkControlMouse : WalkControlKeys;
}

WalkControl startWalkControl(const std::optional<std::string>& saved) {
  if (!saved) {
    return WalkControl::Keys;
  }
  const auto control = parseWalkControl(*saved);
  if (!control) {
    core::logWarning(messages::UnknownSavedWalkControl, *saved);
  }
  return control.value_or(WalkControl::Keys);
}

WalkOrder walkOrderOf(WalkControl control, const WalkDevices& devices) {
  if (control == WalkControl::Keys) {
    return {.stick = devices.keys, .moveTarget = std::nullopt};
  }
  return {.stick = devices.steered.value_or(glm::vec2{0.0F}), .moveTarget = devices.clicked};
}

WalkControlChanger::WalkControlChanger(UserSettings& settings,
                                       std::optional<std::filesystem::path> settingsFile)
    : settings_{&settings}, settingsFile_{std::move(settingsFile)},
      control_{startWalkControl(settings.walkControl)} {}

WalkControl WalkControlChanger::change(const ui::UiValue& value, game::field::Field* field) {
  const std::string* name = std::get_if<std::string>(&value);
  const auto control = name == nullptr ? std::nullopt : parseWalkControl(*name);
  if (!control) {
    return control_;
  }
  use(*control, field);
  settings_->walkControl = std::string{walkControlName(*control)};
  if (settingsFile_) {
    saveUserSettings(*settingsFile_, *settings_);
  }
  return control_;
}

void WalkControlChanger::use(WalkControl control, game::field::Field* field) {
  control_ = control;
  if (field != nullptr) {
    field->dropMoveTarget();
  }
}

} // namespace dwe::app
