#include "chip_reverb.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dwe::audio {

namespace {

// A chip register as a gain: the signed 16-bit value over 0x8000.
constexpr float volume(std::uint16_t value) {
  return static_cast<float>(static_cast<std::int16_t>(value)) / 32768.0F;
}

// A chip address register as an offset in 16-bit samples: it counts 8-byte units.
constexpr std::int32_t address(std::uint16_t value) {
  return static_cast<std::int32_t>(value) * 4;
}

// The registers of the studio preset of medium size (libspu's STUDIO_B), in the order of the
// chip's reverb registers, 1F801DC0h onwards; the public description of the chip lists them.
struct Preset {
  std::int32_t dApf1 = address(0x00B1);
  std::int32_t dApf2 = address(0x007F);
  float vIir = volume(0x70F0);
  float vComb1 = volume(0x4FA8);
  float vComb2 = volume(0xBCE0);
  float vComb3 = volume(0x4510);
  float vComb4 = volume(0xBEF0);
  float vWall = volume(0xB4C0);
  float vApf1 = volume(0x5280);
  float vApf2 = volume(0x4EC0);
  std::int32_t mLSame = address(0x0904);
  std::int32_t mRSame = address(0x076B);
  std::int32_t mLComb1 = address(0x0824);
  std::int32_t mRComb1 = address(0x065F);
  std::int32_t mLComb2 = address(0x07A2);
  std::int32_t mRComb2 = address(0x0616);
  std::int32_t dLSame = address(0x076C);
  std::int32_t dRSame = address(0x05ED);
  std::int32_t mLDiff = address(0x05EC);
  std::int32_t mRDiff = address(0x042E);
  std::int32_t mLComb3 = address(0x050F);
  std::int32_t mRComb3 = address(0x0305);
  std::int32_t mLComb4 = address(0x0462);
  std::int32_t mRComb4 = address(0x02B7);
  std::int32_t dLDiff = address(0x042F);
  std::int32_t dRDiff = address(0x0265);
  std::int32_t mLApf1 = address(0x0264);
  std::int32_t mRApf1 = address(0x01B2);
  std::int32_t mLApf2 = address(0x0100);
  std::int32_t mRApf2 = address(0x0080);
  float vLIn = volume(0x8000);
  float vRIn = volume(0x8000);
};
constexpr Preset Studio{};

// The depth the original sets on both sides (sound.c: initializeMusic): the reverb's output volume.
constexpr float Depth = volume(0x7000);

// Mixer frames per chip tick.
constexpr double TickFrames =
    static_cast<double>(MixSampleRate) / static_cast<double>(ChipReverb::SampleRate);

// The low-pass in front of the chip's rate cuts a little under its Nyquist frequency.
constexpr double LowPassHertz = 10000.0;

struct LowPassCoefficients {
  float b0 = 0.0F;
  float b1 = 0.0F;
  float b2 = 0.0F;
  float a1 = 0.0F;
  float a2 = 0.0F;
};

// A two-pole Butterworth low-pass by the bilinear transform.
LowPassCoefficients lowPassCoefficients() {
  const double k = std::tan(std::numbers::pi * LowPassHertz / static_cast<double>(MixSampleRate));
  const double q = std::numbers::sqrt2 / 2.0;
  const double norm = 1.0 / (1.0 + (k / q) + (k * k));
  const double b0 = k * k * norm;
  return {.b0 = static_cast<float>(b0),
          .b1 = static_cast<float>(2.0 * b0),
          .b2 = static_cast<float>(b0),
          .a1 = static_cast<float>(2.0 * ((k * k) - 1.0) * norm),
          .a2 = static_cast<float>((1.0 - (k / q) + (k * k)) * norm)};
}

const LowPassCoefficients& coefficients() {
  static const LowPassCoefficients value = lowPassCoefficients();
  return value;
}

// What the chip stores: saturated and rounded to 16 bits. The rounding also lets a fading tail
// reach silence or a cycle of the last bits, as on the chip, instead of lingering in denormal
// floats.
float toSample(float value) {
  return std::nearbyint(std::clamp(value, -1.0F, 32767.0F / 32768.0F) * 32768.0F) / 32768.0F;
}

} // namespace

ChipReverb::ChipReverb() noexcept {
  // The coefficients are computed once, off the audio thread's first pass.
  static_cast<void>(coefficients());
}

void ChipReverb::clear() noexcept {
  buffer_.fill(0.0F);
  position_ = 0;
  lowPass_.fill({});
  lastInput_.fill(0.0F);
  untilTick_ = 0.0;
  previousOutput_.fill(0.0F);
  output_.fill(0.0F);
}

void ChipReverb::process(std::span<const float> send, std::span<float> frames) noexcept {
  const LowPassCoefficients& c = coefficients();
  const std::size_t frameCount = std::min(send.size(), frames.size()) / OutputChannels;
  for (std::size_t frame = 0; frame < frameCount; ++frame) {
    std::array<float, OutputChannels> input{};
    for (std::size_t channel = 0; channel < OutputChannels; ++channel) {
      LowPass& filter = lowPass_[channel];
      const float x = send[(frame * OutputChannels) + channel];
      const float y = (c.b0 * x) + (c.b1 * filter.x1) + (c.b2 * filter.x2) - (c.a1 * filter.y1) -
                      (c.a2 * filter.y2);
      filter = {.x1 = x, .x2 = filter.x1, .y1 = y, .y2 = filter.y1};
      input[channel] = y;
    }
    // The chip ticks that fall between the last frame and this one, each on the input at its time.
    while (untilTick_ <= 1.0) {
      const auto at = static_cast<float>(untilTick_);
      tick(lastInput_[0] + ((input[0] - lastInput_[0]) * at),
           lastInput_[1] + ((input[1] - lastInput_[1]) * at));
      untilTick_ += TickFrames;
    }
    untilTick_ -= 1.0;
    lastInput_ = input;
    // This frame's place between the last two ticks.
    const auto between = static_cast<float>(1.0 - (untilTick_ / TickFrames));
    for (std::size_t channel = 0; channel < OutputChannels; ++channel) {
      const float from = previousOutput_[channel];
      frames[(frame * OutputChannels) + channel] += from + ((output_[channel] - from) * between);
    }
  }
}

// One step of the chip's formula: the reflections into the buffer, the four combs, the two
// all-pass filters and the output volume; then the buffer moves on by one sample.
void ChipReverb::tick(float left, float right) noexcept {
  const Preset& p = Studio;
  const float leftIn = p.vLIn * left;
  const float rightIn = p.vRIn * right;

  const auto reflect = [&](std::int32_t target, float in, std::int32_t source) {
    const float before = read(target - 1);
    write(target, ((in + (read(source) * p.vWall) - before) * p.vIir) + before);
  };
  // The same side, then the other side.
  reflect(p.mLSame, leftIn, p.dLSame);
  reflect(p.mRSame, rightIn, p.dRSame);
  reflect(p.mLDiff, leftIn, p.dRDiff);
  reflect(p.mRDiff, rightIn, p.dLDiff);

  float leftOut = (p.vComb1 * read(p.mLComb1)) + (p.vComb2 * read(p.mLComb2)) +
                  (p.vComb3 * read(p.mLComb3)) + (p.vComb4 * read(p.mLComb4));
  float rightOut = (p.vComb1 * read(p.mRComb1)) + (p.vComb2 * read(p.mRComb2)) +
                   (p.vComb3 * read(p.mRComb3)) + (p.vComb4 * read(p.mRComb4));

  const auto allPass = [&](float value, std::int32_t target, std::int32_t delay, float gain) {
    const float delayed = read(target - delay);
    const float stored = toSample(value - (gain * delayed));
    write(target, stored);
    return toSample((stored * gain) + delayed);
  };
  leftOut = allPass(leftOut, p.mLApf1, p.dApf1, p.vApf1);
  rightOut = allPass(rightOut, p.mRApf1, p.dApf1, p.vApf1);
  leftOut = allPass(leftOut, p.mLApf2, p.dApf2, p.vApf2);
  rightOut = allPass(rightOut, p.mRApf2, p.dApf2, p.vApf2);

  previousOutput_ = output_;
  output_ = {leftOut * Depth, rightOut * Depth};
  position_ = (position_ + 1) % BufferSamples;
}

float ChipReverb::read(std::int32_t offset) const noexcept {
  const auto size = static_cast<std::int64_t>(BufferSamples);
  const std::int64_t index = ((static_cast<std::int64_t>(position_) + offset) % size + size) % size;
  return buffer_[static_cast<std::size_t>(index)];
}

void ChipReverb::write(std::int32_t offset, float value) noexcept {
  const auto size = static_cast<std::int64_t>(BufferSamples);
  const std::int64_t index = ((static_cast<std::int64_t>(position_) + offset) % size + size) % size;
  buffer_[static_cast<std::size_t>(index)] = toSample(value);
}

} // namespace dwe::audio
