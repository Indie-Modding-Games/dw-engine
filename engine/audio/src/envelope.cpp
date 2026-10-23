#include "envelope.hpp"

#include <dwe/audio/mixing.hpp>

#include <algorithm>

namespace dwe::audio {

namespace {

constexpr double ChipRate = 44100.0;
constexpr double TicksPerFrame = ChipRate / static_cast<double>(MixSampleRate);
constexpr std::int32_t MaxLevel = 0x7FFF;
constexpr std::int32_t SustainLevelUnit = 0x800;
// Above this level an exponential attack slows down to a quarter of its pace.
constexpr std::int32_t ExponentialAttackKnee = 0x6000;

constexpr std::uint8_t bits(std::uint16_t value, int low, int count) {
  return static_cast<std::uint8_t>((value >> low) & ((1U << count) - 1U));
}

} // namespace

Envelope::Envelope(ChipEnvelope registers)
    : registers_(registers), sustainLevel_((bits(registers.adsr1, 0, 4) + 1) * SustainLevelUnit) {
  enter(Phase::Attack);
}

float Envelope::advance() noexcept {
  tickDebt_ += TicksPerFrame;
  while (tickDebt_ >= 1.0) {
    tickDebt_ -= 1.0;
    tick();
  }
  return static_cast<float>(level_) / static_cast<float>(MaxLevel);
}

Envelope::Rate Envelope::rate() const noexcept {
  const std::uint16_t first = registers_.adsr1;
  const std::uint16_t second = registers_.adsr2;
  switch (phase_) {
  case Phase::Attack:
    return {.shift = bits(first, 10, 5),
            .step = bits(first, 8, 2),
            .decreasing = false,
            .exponential = bits(first, 15, 1) != 0};
  case Phase::Decay:
    return {.shift = bits(first, 4, 4), .step = 0, .decreasing = true, .exponential = true};
  case Phase::Sustain:
  case Phase::Done:
    break;
  }
  return {.shift = bits(second, 8, 5),
          .step = bits(second, 6, 2),
          .decreasing = bits(second, 14, 1) != 0,
          .exponential = bits(second, 15, 1) != 0};
}

void Envelope::enter(Phase phase) noexcept {
  phase_ = phase;
  waitTicks_ = 0;
}

void Envelope::tick() noexcept {
  if (phase_ == Phase::Done) {
    return;
  }
  if (waitTicks_ > 0) {
    --waitTicks_;
    return;
  }
  // The top sustain level (0x8000) is above the loudest level, so the decay is skipped.
  if (phase_ == Phase::Decay && level_ <= sustainLevel_) {
    enter(Phase::Sustain);
  }
  const Rate current = rate();
  const int slow = std::max(0, current.shift - 11);
  const int fast = std::max(0, 11 - current.shift);
  std::int32_t cycles = 1 << slow;
  std::int32_t step = current.decreasing ? -8 + current.step : 7 - current.step;
  step *= 1 << fast;
  if (current.exponential && !current.decreasing && level_ > ExponentialAttackKnee) {
    cycles *= 4;
  }
  if (current.exponential && current.decreasing) {
    // Shifting rounds toward minus infinity, so the level still reaches silence at the bottom.
    step = (step * level_) >> 15;
  }
  waitTicks_ = cycles - 1;
  level_ = std::clamp(level_ + step, 0, MaxLevel);

  if (phase_ == Phase::Attack && level_ == MaxLevel) {
    enter(Phase::Decay);
  } else if (phase_ == Phase::Decay && level_ <= sustainLevel_) {
    enter(Phase::Sustain);
  } else if (phase_ == Phase::Sustain && current.decreasing && level_ == 0) {
    phase_ = Phase::Done;
  }
}

} // namespace dwe::audio
