#include <dwe/game/battle/battle.hpp>
#include <dwe/screens/death_countdown.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <string>

namespace dwe::screens {
namespace {

// BTL_tickDeathCountdown: the step starts over on the timer's multiples of 31; the digit is
// (timer + 29) / 30 frames in.
constexpr std::int32_t Cycle = 31;
constexpr std::int32_t DigitFrames = 30;
constexpr std::int32_t FirstDigit = 5;
// Steps 1 to 20 pop the digit and turn the palette; from 21 the sign shrinks and spins.
constexpr std::int32_t LastPopStep = 20;
constexpr std::int32_t LastGrowStep = 4;
constexpr std::int32_t LastShrinkStep = 7;
constexpr std::int32_t PaletteSteps = 3;
constexpr std::int32_t BadgePalettes = 3;
// Scales in 4096ths (GsSPRITE scalex): the digit grows 0x199 a step; the shrink takes 5 % a step.
constexpr double Unit = 4096.0;
constexpr std::int32_t PopStep = 0x199;
constexpr std::int32_t ShrinkPercent = 5;
constexpr std::int32_t Hundred = 100;
// 0x40000 of GsSPRITE rotate a step: 4096 to the degree.
constexpr double TurnDegrees = 64.0;

// BTL_addDeathCountdown: getEntityScreenPos counts from the 240-line screen's middle; the sign
// stays within 140 pixels of it across and 100 up or down.
constexpr double ScreenLines = 240.0;
constexpr double Middle = 0.5;
constexpr std::int32_t Across = 140;
constexpr std::int32_t UpOrDown = 100;

constexpr double Percent = 100.0;

std::int32_t stepOf(std::int32_t timer) {
  return timer < Cycle ? timer : (timer % Cycle) + 1;
}

double digitScaleOf(std::int32_t step) {
  if (step <= LastGrowStep) {
    return (Unit + static_cast<double>((step - 1) * PopStep)) / Unit;
  }
  if (step <= LastShrinkStep) {
    return (Unit + static_cast<double>((LastShrinkStep - step) * PopStep)) / Unit;
  }
  return 1.0;
}

} // namespace

std::optional<DeathCountdownLook> deathCountdownLookOf(std::int32_t timer) {
  if (timer < 1 || timer > DeathCountdownLastFrame) {
    return std::nullopt;
  }
  const std::int32_t step = stepOf(timer);
  const std::int32_t frame = (timer + DigitFrames - 1) / DigitFrames;
  DeathCountdownLook look{.digit = std::max(1, FirstDigit + 1 - frame)};
  // Every third step of 1 to 20 moves the badge to its next palette; a cycle moves it six times, so
  // each cycle starts on the first one.
  look.badgePalette = (std::min(step, LastPopStep) / PaletteSteps) % BadgePalettes;
  if (step <= LastPopStep) {
    look.digitScale = digitScaleOf(step);
    return look;
  }
  const std::int32_t shrink = step - LastPopStep;
  const auto scale =
      static_cast<double>(((Hundred - (shrink * ShrinkPercent)) * static_cast<std::int32_t>(Unit)) /
                          Hundred) /
      Unit;
  look.digitScale = scale;
  look.badgeScale = scale;
  look.turn = static_cast<double>(shrink) * TurnDegrees;
  return look;
}

std::optional<std::int32_t> deathCountdownTimerOf(const game::battle::Battle& battle) {
  const game::battle::BattleState& state = battle.state();
  const std::int32_t shownAt = battle.rules().ending.reviveCountdownShownAt;
  if (state.matchClock || !state.partner().fainted || state.deathFrames < shownAt) {
    return std::nullopt;
  }
  // Suposición: the countdown ticks on the frame it is added, so that frame is its first.
  const std::int32_t timer = state.deathFrames - shownAt + 1;
  if (timer > DeathCountdownLastFrame) {
    return std::nullopt;
  }
  return timer;
}

WindowFraction deathCountdownAnchorOf(WindowFraction node, double aspect) {
  const double across = ScreenLines * aspect;
  const auto x = std::clamp(static_cast<std::int32_t>(std::lround((node.x - Middle) * across)),
                            -Across, Across);
  const auto y = std::clamp(static_cast<std::int32_t>(std::lround((node.y - Middle) * ScreenLines)),
                            -UpOrDown, UpOrDown);
  return {.x = Middle + (static_cast<double>(x) / across),
          .y = Middle + (static_cast<double>(y) / ScreenLines)};
}

ScreenSettings settingsOf(const std::optional<DeathCountdownLook>& look, WindowFraction anchor) {
  std::string text;
  if (look) {
    std::format_to(std::back_inserter(text), "{:.2f} {:.2f} {} {:.3f} {:.3f} {:.1f} {}",
                   anchor.x * Percent, anchor.y * Percent, look->digit, look->digitScale,
                   look->badgeScale, look->turn, look->badgePalette);
  }
  return {{.name = std::string{DeathCountdownSetting}, .value = std::move(text)}};
}

} // namespace dwe::screens
