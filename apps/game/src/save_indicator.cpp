#include "save_indicator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

#include "user_settings.hpp"

namespace dwe::app {
namespace {

// The indicator's element in its document.
constexpr std::string_view IndicatorElement = "indicator";
// The space kept between the indicator and a window, in dp: two of the original's pixels.
constexpr float GapDp = 6.0F;
// Opacity steps published while it fades: the document is styled again only when one changes.
constexpr float OpacitySteps = 32.0F;

ui::Ui::Rect raised(const ui::Ui::Rect& rect, float lift) {
  return {
      .left = rect.left, .top = rect.top - lift, .right = rect.right, .bottom = rect.bottom - lift};
}

bool near(const ui::Ui::Rect& a, const ui::Ui::Rect& b, float gap) {
  return a.left < b.right + gap && b.left < a.right + gap && a.top < b.bottom + gap &&
         b.top < a.bottom + gap;
}

bool clearOf(const ui::Ui::Rect& rect, std::span<const ui::Ui::Rect> drawn, float gap) {
  return std::ranges::none_of(drawn,
                              [&](const ui::Ui::Rect& other) { return near(rect, other, gap); });
}

} // namespace

void SaveNotice::advance(double seconds) {
  left_ = std::max(0.0, left_ - std::max(0.0, seconds));
}

float SaveNotice::opacity() const {
  if (left_ <= 0.0) {
    return 0.0F;
  }
  return static_cast<float>(std::min(1.0, left_ / FadeSeconds));
}

namespace {

// From lap `lap - 1`'s gait to lap `lap`'s: the other one, except that a lap whose hash (Knuth's
// multiplicative one) has its two top bits clear keeps it, about one in four, but never right
// after a lap that kept it. `kept` says whether the last lap kept its gait, and is updated.
LaneRunner::Gait nextGait(std::uint32_t lap, LaneRunner::Gait last, bool& kept) {
  kept = !kept && ((lap * 2654435761U) >> 30U) == 0;
  if (kept) {
    return last;
  }
  return last == LaneRunner::Gait::Run ? LaneRunner::Gait::Walk : LaneRunner::Gait::Run;
}

} // namespace

LaneRunner::Gait LaneRunner::gaitOfLap(std::uint32_t lap) {
  Gait gait = Gait::Run;
  bool kept = false;
  for (std::uint32_t at = 1; at <= lap; ++at) {
    gait = nextGait(at, gait, kept);
  }
  return gait;
}

void LaneRunner::advance(float seconds) {
  seconds = std::max(0.0F, seconds);
  while (seconds > 0.0F) {
    if (turnLeft_ > 0.0F) {
      const float step = std::min(seconds, turnLeft_);
      turnLeft_ -= step;
      seconds -= step;
      continue;
    }
    const float speed = speedOf(gait_);
    const float end = direction_ * halfLength_;
    const float toEnd = (end - x_) * direction_;
    const float step = std::min(seconds, toEnd / speed);
    x_ += direction_ * speed * step;
    seconds -= step;
    if (step * speed >= toEnd) {
      // At the end: turn round there, onto the next lap.
      x_ = end;
      direction_ = -direction_;
      turnLeft_ = TurnSeconds;
      ++lap_;
      gait_ = nextGait(lap_, gait_, kept_);
    }
  }
}

float LaneRunner::yaw() const {
  constexpr float Quarter = std::numbers::pi_v<float> / 2.0F;
  // Turning to face `direction_`, from the other way through facing the viewer (yaw 0).
  const float done = 1.0F - (turnLeft_ / TurnSeconds);
  return direction_ * Quarter * (turnLeft_ > 0.0F ? (2.0F * done) - 1.0F : 1.0F);
}

std::optional<float> liftClearOf(const ui::Ui::Rect& atCorner, std::span<const ui::Ui::Rect> drawn,
                                 float gap, float top) {
  std::vector<float> lifts{0.0F};
  for (const ui::Ui::Rect& other : drawn) {
    // Its bottom `gap` above the other's top.
    const float lift = atCorner.bottom - other.top + gap;
    if (lift > 0.0F) {
      lifts.push_back(lift);
    }
  }
  std::ranges::sort(lifts);
  for (const float lift : lifts) {
    const ui::Ui::Rect rect = raised(atCorner, lift);
    if (rect.top < top) {
      break;
    }
    if (clearOf(rect, drawn, gap)) {
      return lift;
    }
  }
  return std::nullopt;
}

SaveIndicator::SaveIndicator(ui::Ui& ui, std::uint64_t savesWritten, bool enabled)
    : ui_(&ui), savesSeen_(savesWritten), enabled_(enabled) {
  ui_->setSetting(SaveNoticeSetting, enabled_);
  showText(SavingText);
  publish(0.0F, 0.0F);
  ui_->show(SaveIndicatorDocument);
}

SaveIndicator::~SaveIndicator() {
  ui_->hide(SaveIndicatorDocument);
  ui_->setSetting(SaveIndicatorShownSetting, false);
}

void SaveIndicator::setEnabled(bool enabled) {
  enabled_ = enabled;
  ui_->setSetting(SaveNoticeSetting, enabled_);
  if (!enabled_) {
    notice_ = {};
  }
}

void SaveIndicator::noteSkippedAutosaves(std::uint64_t autosavesSkipped) {
  const bool skipped = skipsSeen_ && autosavesSkipped != *skipsSeen_;
  skipsSeen_ = autosavesSkipped;
  if (skipped && enabled_) {
    showText(AutosaveNoSlotText);
    notice_.start();
    placed_ = false;
  }
}

void SaveIndicator::showText(std::string_view key) {
  if (key != text_) {
    text_ = key;
    ui_->setSetting(SaveIndicatorTextSetting, std::string{key});
  }
}

void SaveIndicator::update(std::uint64_t savesWritten, double seconds) {
  const bool saved = savesWritten != savesSeen_;
  savesSeen_ = savesWritten;
  if (saved && enabled_) {
    showText(SavingText);
    notice_.start();
    placed_ = false;
  } else {
    notice_.advance(seconds);
  }
  if (!notice_.active()) {
    shown_ = false;
    publish(0.0F, lift_);
    return;
  }
  // The last frame's layout, with the lift it was laid out with taken back off: the corner.
  if (const std::optional<ui::Ui::Rect> rect =
          ui_->elementRect(SaveIndicatorDocument, IndicatorElement)) {
    const ui::Ui::Rect corner = raised(*rect, -std::max(publishedLift_, 0.0F));
    const std::vector<ui::Ui::Rect> drawn = ui_->drawnRects(SaveIndicatorDocument);
    const float gap = GapDp * ui_->dpRatio();
    // It moves only when a window comes over it, so it does not follow a sliding window down.
    if (!placed_ || !clearOf(raised(corner, lift_), drawn, gap)) {
      const std::optional<float> lift = liftClearOf(corner, drawn, gap, gap);
      placed_ = lift.has_value();
      lift_ = lift.value_or(0.0F);
    }
  } else {
    placed_ = false;
  }
  shown_ = placed_;
  publish(shown_ ? notice_.opacity() : 0.0F, lift_);
}

void SaveIndicator::publish(float opacity, float lift) {
  const float stepped = std::ceil(opacity * OpacitySteps) / OpacitySteps;
  const float rounded = std::round(lift);
  if (rounded != publishedLift_) {
    publishedLift_ = rounded;
    ui_->setSetting(SaveIndicatorLiftSetting, static_cast<double>(rounded));
  }
  if (stepped != publishedOpacity_) {
    publishedOpacity_ = stepped;
    ui_->setSetting(SaveIndicatorOpacitySetting, static_cast<double>(stepped));
  }
  ui_->setSetting(SaveIndicatorShownSetting, stepped > 0.0F);
}

std::optional<bool> changeAutosave(const ui::UiValue& value, ui::Ui& ui, UserSettings& settings,
                                   const std::optional<std::filesystem::path>& settingsFile) {
  const bool* autosave = std::get_if<bool>(&value);
  if (autosave == nullptr) {
    return std::nullopt;
  }
  ui.setSetting(AutosaveSetting, *autosave);
  settings.autosave = *autosave;
  if (settingsFile) {
    saveUserSettings(*settingsFile, settings);
  }
  return *autosave;
}

} // namespace dwe::app
