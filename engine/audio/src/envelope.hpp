#pragma once

#include <dwe/audio/voice.hpp>

#include <cstdint>

namespace dwe::audio {

// The sound chip's volume envelope, stepped at the chip's own 44.1 kHz and read once per mix frame.
// It follows the chip's public register documentation: each phase adds a step every so many ticks,
// with exponential phases changing their pace with the level. A voice is never released here (an
// effect stops with the mixer's quick fade, as on the console), so the envelope ends only when a
// falling sustain reaches silence.
class Envelope {
public:
  Envelope() = default;
  explicit Envelope(ChipEnvelope registers);

  // The level for the next mix frame, from 0 to 1.
  [[nodiscard]] float advance() noexcept;
  [[nodiscard]] bool silentForGood() const { return phase_ == Phase::Done; }

private:
  enum class Phase : std::uint8_t { Attack, Decay, Sustain, Done };

  struct Rate {
    std::uint8_t shift = 0;
    std::uint8_t step = 0;
    bool decreasing = false;
    bool exponential = false;
  };

  void tick() noexcept;
  void enter(Phase phase) noexcept;
  [[nodiscard]] Rate rate() const noexcept;

  ChipEnvelope registers_;
  Phase phase_ = Phase::Done;
  std::int32_t level_ = 0;
  std::int32_t sustainLevel_ = 0;
  std::int32_t waitTicks_ = 0;
  double tickDebt_ = 0.0;
};

} // namespace dwe::audio
