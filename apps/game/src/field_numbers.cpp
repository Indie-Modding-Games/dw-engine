#include "field_numbers.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstddef>

namespace dwe::app {

namespace {

// The partner's queue (addEntityText's slot 0).
constexpr std::size_t Partner = 0;
// Three ticks of 1/60 s add up to a hair under the field frame's 0.05 s float: a frame is due that
// close to its end.
constexpr double Tolerance = 1.0e-6;

} // namespace

FieldNumbers::FieldNumbers(ui::Ui& ui, float frameSeconds)
    : ui_(&ui), frameSeconds_(frameSeconds > 0.0F ? frameSeconds : 1.0F) {}

FieldNumbers::~FieldNumbers() {
  if (visible_) {
    ui_->hide(FieldNumbersDocument);
  }
}

void FieldNumbers::add(std::span<const std::int32_t> damage,
                       std::optional<screens::WindowFraction> feet) {
  if (damage.empty()) {
    return;
  }
  for (const std::int32_t value : damage) {
    numbers_.add(Partner, value, screens::NumberColour::Plain, feet);
    core::logDebug("[port:field] addEntityText (entity_text.c:96): partner value={}{}", value,
                   feet ? "" : " off the camera");
  }
  // renderEntityText runs as an object of the frame that added it: the number shows at once, and
  // the next frame moves it on.
  seconds_ = 0.0;
  step();
}

void FieldNumbers::tick(double seconds) {
  if (!active()) {
    seconds_ = 0.0;
    return;
  }
  seconds_ += seconds;
  while (seconds_ + Tolerance >= frameSeconds_) {
    seconds_ -= frameSeconds_;
    step();
  }
}

void FieldNumbers::clear() {
  numbers_.clear();
  seconds_ = 0.0;
  publish();
}

void FieldNumbers::step() {
  numbers_.step();
  publish();
}

void FieldNumbers::publish() {
  screens::publish(*ui_, screens::settingsOf(numbers_.shown(), screens::FieldNumbersSetting),
                   screens::FieldNumbersRevisionSetting);
  const bool shows = !numbers_.shown().empty();
  if (shows != visible_) {
    visible_ = shows;
    if (shows) {
      ui_->show(FieldNumbersDocument);
    } else {
      ui_->hide(FieldNumbersDocument);
    }
  }
}

} // namespace dwe::app
