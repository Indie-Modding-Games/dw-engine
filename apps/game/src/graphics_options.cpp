#include "graphics_options.hpp"

#include <dwe/core/log.hpp>

#include <ranges>
#include <utility>
#include <variant>

#include "messages.hpp"

namespace dwe::app {

namespace {

constexpr std::string_view ShadowsOff = "off";
constexpr std::string_view ShadowsRound = "round";
constexpr std::string_view ShadowsSun = "sun";

std::optional<bool> onOff(std::string_view value) {
  if (value == "on") {
    return true;
  }
  if (value == "off") {
    return false;
  }
  return std::nullopt;
}

} // namespace

std::optional<ShadowMode> parseShadowMode(std::string_view name) {
  if (name == ShadowsOff) {
    return ShadowMode::Off;
  }
  if (name == ShadowsRound) {
    return ShadowMode::Round;
  }
  if (name == ShadowsSun) {
    return ShadowMode::Sun;
  }
  return std::nullopt;
}

std::string_view shadowModeName(ShadowMode mode) {
  switch (mode) {
  case ShadowMode::Off:
    return ShadowsOff;
  case ShadowMode::Round:
    return ShadowsRound;
  case ShadowMode::Sun:
    return ShadowsSun;
  }
  return ShadowsSun;
}

GraphicsOptions startGraphicsOptions(const UserSettings& settings) {
  GraphicsOptions options;
  if (settings.shadows) {
    if (const auto mode = parseShadowMode(*settings.shadows)) {
      options.shadows = *mode;
    } else {
      core::logWarning(messages::UnknownSavedShadows, *settings.shadows);
    }
  }
  options.enhancedLighting = settings.enhancedLighting.value_or(true);
  options.vividColors = settings.vividColors.value_or(true);
  return options;
}

std::optional<GraphicsOptions> withGraphicsFlag(GraphicsOptions options, std::string_view text) {
  for (const auto part : std::views::split(text, ',')) {
    const std::string_view item{part};
    const std::size_t equals = item.find('=');
    if (equals == std::string_view::npos) {
      return std::nullopt;
    }
    const std::string_view key = item.substr(0, equals);
    const std::string_view value = item.substr(equals + 1);
    if (key == "shadows") {
      const auto mode = parseShadowMode(value);
      if (!mode) {
        return std::nullopt;
      }
      options.shadows = *mode;
    } else if (key == "lighting" && onOff(value)) {
      options.enhancedLighting = *onOff(value);
    } else if (key == "colors" && onOff(value)) {
      options.vividColors = *onOff(value);
    } else {
      return std::nullopt;
    }
  }
  return options;
}

GraphicsChanger::GraphicsChanger(UserSettings& settings,
                                 std::optional<std::filesystem::path> settingsFile,
                                 GraphicsOptions start)
    : settings_{&settings}, settingsFile_{std::move(settingsFile)}, options_{start} {}

bool GraphicsChanger::handle(const ui::UiCommand& command, ui::Ui& ui) {
  if (command.name == ShadowsSetting) {
    const std::string* name = std::get_if<std::string>(&command.value);
    if (const auto mode = name == nullptr ? std::nullopt : parseShadowMode(*name)) {
      options_.shadows = *mode;
      settings_->shadows = std::string{shadowModeName(*mode)};
      save();
    }
  } else if (command.name == EnhancedLightingSetting) {
    if (const bool* on = std::get_if<bool>(&command.value)) {
      options_.enhancedLighting = *on;
      settings_->enhancedLighting = *on;
      save();
    }
  } else if (command.name == VividColorsSetting) {
    if (const bool* on = std::get_if<bool>(&command.value)) {
      options_.vividColors = *on;
      settings_->vividColors = *on;
      save();
    }
  } else {
    return false;
  }
  publish(ui);
  return true;
}

void GraphicsChanger::publish(ui::Ui& ui) const {
  ui.setSetting(ShadowsSetting, std::string{shadowModeName(options_.shadows)});
  ui.setSetting(EnhancedLightingSetting, options_.enhancedLighting);
  ui.setSetting(VividColorsSetting, options_.vividColors);
}

void GraphicsChanger::save() {
  if (settingsFile_) {
    saveUserSettings(*settingsFile_, *settings_);
  }
}

} // namespace dwe::app
