#pragma once

#include <dwe/audio/mixing.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dwe::audio {

// The console sound chip's reverb as the original sets it at boot and never changes (sound.c:
// initializeMusic): the studio preset of medium size, at depth 0x7000 on both sides. The chip runs
// a stereo Schroeder reverberator at 22 050 Hz over a ring buffer of 16-bit samples, from the
// voices that have their reverb bit set; this one follows the public description of the chip's
// registers and formula (docs/architecture/audio.md, "Reverberación"), in float, with the same
// saturation to 16 bits when it writes its buffer. Only the resampling between the mixer's 48 kHz
// and the chip's 22 050 Hz is an approximation (a low-pass and linear steps instead of the chip's
// 39-tap filter). It never allocates, locks or logs.
class ChipReverb {
public:
  // The chip's reverb rate.
  static constexpr std::uint32_t SampleRate = 22050;

  ChipReverb() noexcept;

  // Adds the reverb of `send` (the voices that go to the reverb, interleaved stereo at the mixer's
  // rate) to `frames`, which is as long.
  void process(std::span<const float> send, std::span<float> frames) noexcept;
  // Silences the buffer, as the original clears its work area when it sets the mode.
  void clear() noexcept;

private:
  // The work area of the preset in 16-bit samples: 0x4840 bytes.
  static constexpr std::size_t BufferSamples = 0x4840 / 2;

  void tick(float left, float right) noexcept;
  [[nodiscard]] float read(std::int32_t offset) const noexcept;
  void write(std::int32_t offset, float value) noexcept;

  std::array<float, BufferSamples> buffer_{};
  std::size_t position_ = 0;

  // The low-pass in front of the chip's rate, one per side: a two-pole Butterworth filter.
  struct LowPass {
    float x1 = 0.0F;
    float x2 = 0.0F;
    float y1 = 0.0F;
    float y2 = 0.0F;
  };
  std::array<LowPass, OutputChannels> lowPass_{};
  // The last filtered input frame, the time of the next chip tick in mixer frames from it, and the
  // last two outputs of the chip, between which the mixer's frames step.
  std::array<float, OutputChannels> lastInput_{};
  double untilTick_ = 0.0;
  std::array<float, OutputChannels> previousOutput_{};
  std::array<float, OutputChannels> output_{};
};

} // namespace dwe::audio
