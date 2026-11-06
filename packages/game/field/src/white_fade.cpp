#include <dwe/core/log.hpp>
#include <dwe/game/field/white_fade.hpp>

#include <algorithm>

namespace dwe::game::field {

namespace {

// FADE_PROGRESS is one byte: renderFadeOut's sum wraps before it is compared with shadeMax.
constexpr std::int32_t ProgressMask = 0xFF;

std::int32_t stepFor(const TransitionRules& rules, std::int32_t frames) {
  return rules.shadeMax / (std::max(frames, 0) + 1);
}

} // namespace

void WhiteFade::fadeOut(std::int32_t frames, std::int32_t from) {
  core::logDebug("[port:field] fadeToWhite (fade.c:170): {} frames from shade {}", frames, from);
  mode_ = Mode::Out;
  step_ = stepFor(rules_, frames);
  progress_ = from;
  drawOut();
}

void WhiteFade::fadeIn(std::int32_t frames) {
  core::logDebug("[port:field] fadeFromWhite (fade.c:193): {} frames", frames);
  mode_ = Mode::In;
  step_ = stepFor(rules_, frames);
  frames_ = std::max(frames, 0);
  drawn_ = 0;
  drawIn();
}

void WhiteFade::frame() {
  if (mode_ == Mode::Out) {
    drawOut();
  } else if (mode_ == Mode::In) {
    drawIn();
  }
}

void WhiteFade::drawOut() {
  // renderFadeOut: FADE_PROGRESS += step, held at shadeMax.
  const std::int32_t next = (progress_ + step_) & ProgressMask;
  progress_ = std::min(next, rules_.shadeMax);
}

void WhiteFade::drawIn() {
  // renderFadeIn: while FADE_IN_CURRENT < frames + 1 it draws shadeMax - step * FADE_IN_CURRENT;
  // on the frame after it removes itself without drawing.
  ++drawn_;
  if (drawn_ > frames_) {
    mode_ = Mode::None;
    return;
  }
  // A fade longer than shadeMax frames has no step: it takes one shade a frame instead.
  const std::int32_t taken = step_ * drawn_ < 1 ? drawn_ : step_ * drawn_;
  progress_ = (rules_.shadeMax - taken) & ProgressMask;
}

float WhiteFade::whiteness() const {
  return active() ? shadeDarkness(rules_, progress_) : 0.0F;
}

} // namespace dwe::game::field
