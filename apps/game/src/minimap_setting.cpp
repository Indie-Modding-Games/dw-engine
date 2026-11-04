#include "minimap_setting.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <utility>
#include <variant>
#include <vector>

#include "messages.hpp"

namespace dwe::app {

std::string_view minimapModeOf(const ui::UiValue& value) {
  if (const bool* visible = std::get_if<bool>(&value)) {
    return *visible ? MinimapOn : MinimapOff;
  }
  if (const std::string* mode = std::get_if<std::string>(&value);
      mode != nullptr && *mode == MinimapOff) {
    return MinimapOff;
  }
  return MinimapOn;
}

std::string_view startMinimapMode(const std::optional<std::string>& saved) {
  if (!saved) {
    return MinimapOn;
  }
  if (*saved != MinimapOn && *saved != MinimapOff && *saved != MinimapAuto) {
    core::logWarning(messages::UnknownSavedMinimap, *saved);
  }
  return minimapModeOf(ui::UiValue{*saved});
}

bool minimapAllowed(const MinimapScene& scene) {
  return scene.fieldRunning && !scene.battle && !scene.minigame;
}

MinimapChanger::MinimapChanger(UserSettings& settings,
                               std::optional<std::filesystem::path> settingsFile)
    : settings_{&settings}, settingsFile_{std::move(settingsFile)} {}

std::string_view MinimapChanger::change(const ui::UiValue& value) {
  const std::string_view mode = minimapModeOf(value);
  settings_->minimap = std::string{mode};
  if (settingsFile_) {
    saveUserSettings(*settingsFile_, *settings_);
  }
  return mode;
}

namespace {

bool overlaps(const ui::Ui::Rect& a, const ui::Ui::Rect& b) {
  return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

} // namespace

bool MinimapCover::update(const std::optional<ui::Ui::Rect>& minimap,
                          std::span<const ui::Ui::Rect> drawn, double seconds) {
  const bool laidOut = minimap && minimap->right > minimap->left && minimap->bottom > minimap->top;
  const bool under = laidOut && std::ranges::any_of(drawn, [&](const ui::Ui::Rect& other) {
                       return overlaps(*minimap, other);
                     });
  if (under) {
    covered_ = true;
    clearSeconds_ = 0.0;
  } else if (covered_) {
    clearSeconds_ += std::max(0.0, seconds);
    covered_ = laidOut && clearSeconds_ < HoldSeconds;
  }
  return covered_;
}

void MinimapCover::update(ui::Ui& ui, double seconds) {
  const std::vector<ui::Ui::Rect> drawn = ui.drawnRects(HudDocument);
  const bool covered = update(ui.elementRect(HudDocument, MinimapPanel), drawn, seconds);
  if (published_ != covered) {
    published_ = covered;
    ui.setSetting(MinimapCoveredSetting, covered);
  }
}

} // namespace dwe::app
