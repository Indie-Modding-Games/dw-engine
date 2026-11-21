#include "mode_options.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

#include "messages.hpp"

namespace dwe::app {

namespace {

core::Result<UserSettings> withPins(const UserSettings& settings,
                                    const std::vector<game::modes::OptionPin>& pins) {
  auto text = formatUserSettings(settings);
  if (!text) {
    return std::unexpected{text.error()};
  }
  auto tree = core::parseYaml(*text);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  core::YamlNode root = tree->isMapping() ? std::move(*tree) : core::YamlNode::mapping();
  for (const game::modes::OptionPin& pin : pins) {
    root.set(pin.option, core::YamlNode::scalar(pin.value));
  }
  auto emitted = core::emitYaml(root);
  if (!emitted) {
    return std::unexpected{emitted.error()};
  }
  return parseUserSettings(*emitted);
}

} // namespace

ui::UiValue pinnedUiValue(std::string_view text) {
  if (text == "true") {
    return true;
  }
  if (text == "false") {
    return false;
  }
  std::int64_t number = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
  if (error == std::errc{} && end == text.data() + text.size()) {
    return static_cast<double>(number);
  }
  return std::string{text};
}

ModeOptions::ModeOptions(const game::modes::GameMode* mode, ui::Ui* ui) : ui_(ui) {
  if (mode != nullptr) {
    for (const game::modes::OptionPin& pin : mode->options) {
      if (!std::ranges::contains(FixableOptions, std::string_view{pin.option})) {
        core::logWarning(messages::ModeOptionRefused, mode->id, pin.option, pin.value,
                         messages::ModeOptionNotFixable);
        continue;
      }
      if (auto checked = withPins({}, {pin}); !checked) {
        core::logWarning(messages::ModeOptionRefused, mode->id, pin.option, pin.value,
                         checked.error().message);
        continue;
      }
      pins_.push_back(pin);
    }
    if (!pins_.empty()) {
      std::string names;
      for (const game::modes::OptionPin& pin : pins_) {
        names += names.empty() ? pin.option : ", " + pin.option;
      }
      core::logInfo(messages::ModeOptionsFixed, mode->id, names);
    }
  }
  if (ui_ == nullptr) {
    return;
  }
  ui_->setSetting(OptionsVariantSetting, mode != nullptr ? mode->nameKey : std::string{});
  for (const game::modes::OptionPin& pin : pins_) {
    before_.emplace_back(pin.option, ui_->setting(pin.option));
    ui_->setSetting(pin.option, pinnedUiValue(pin.value));
    ui_->setSetting(std::string{OptionsLockedPrefix} + pin.option, true);
  }
}

ModeOptions::~ModeOptions() {
  if (ui_ == nullptr) {
    return;
  }
  ui_->setSetting(OptionsVariantSetting, std::string{});
  for (auto& [option, value] : before_) {
    ui_->setSetting(option, std::move(value));
    ui_->setSetting(std::string{OptionsLockedPrefix} + option, false);
  }
}

bool ModeOptions::fixes(std::string_view option) const {
  return std::ranges::contains(pins_, option, &game::modes::OptionPin::option);
}

UserSettings ModeOptions::over(const UserSettings& settings) const {
  if (pins_.empty()) {
    return settings;
  }
  auto pinned = withPins(settings, pins_);
  if (!pinned) {
    core::logWarning(messages::ModeOptionsNotApplied, pinned.error().message);
    return settings;
  }
  return std::move(*pinned);
}

bool ModeOptions::refuse(std::string_view command) {
  const auto pin = std::ranges::find(pins_, command, &game::modes::OptionPin::option);
  if (pin == pins_.end()) {
    return false;
  }
  if (ui_ != nullptr) {
    ui_->setSetting(pin->option, pinnedUiValue(pin->value));
  }
  core::logInfo(messages::ModeOptionLocked, pin->option, pin->value);
  return true;
}

} // namespace dwe::app
