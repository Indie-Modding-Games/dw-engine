#include <dwe/formats/audio/envelope.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace dwe::formats::audio {

namespace {

constexpr double SampleRate = 44100.0;
constexpr std::int32_t MaxLevel = 0x7FFF;
constexpr std::int32_t SustainLevelUnit = 0x800;
constexpr std::int32_t ExponentialSlowdownLevel = 0x6000;
constexpr unsigned UnitShift = 11;
constexpr std::int32_t RiseBase = 7;
constexpr std::int32_t FallBase = -8;
constexpr std::uint8_t FrozenSustainRate = 0x7F;
constexpr std::int16_t MinTimecents = -12000;
constexpr std::int16_t MaxTimecents = 8000;
constexpr std::int16_t SilenceCentibels = 1000; // full attenuation, by the format's convention
constexpr std::int16_t MaxCentibels = 1440;
constexpr double FullFallDecibels = 100.0;
constexpr std::int32_t MeasureFallRatio = 1000; // falls are measured down 60 dB and scaled
constexpr std::size_t MaxSteps = std::size_t{1} << 20U;

struct Phase {
  unsigned shift = 0;
  std::int32_t step = 0;
  bool exponential = false;
  bool decreasing = false;
};

struct PhaseRun {
  std::uint64_t samples = 0;
  std::int32_t level = 0;
};

// Steps a phase from a level until it reaches the target, and returns how long that took.
PhaseRun runPhase(const Phase& phase, std::int32_t level, std::int32_t target) {
  const unsigned slower = phase.shift > UnitShift ? phase.shift - UnitShift : 0;
  const unsigned larger = phase.shift < UnitShift ? UnitShift - phase.shift : 0;
  const std::int32_t baseStep = phase.step * (1 << larger);
  std::uint64_t samples = 0;
  for (std::size_t i = 0; i < MaxSteps; ++i) {
    if (phase.decreasing ? level <= target : level >= target) {
      break;
    }
    std::uint64_t cycles = std::uint64_t{1} << slower;
    std::int32_t step = baseStep;
    if (phase.exponential && !phase.decreasing && level > ExponentialSlowdownLevel) {
      cycles *= 4;
    }
    if (phase.exponential && phase.decreasing) {
      step = (step * level) >> 15;
    }
    level = std::clamp(level + step, 0, MaxLevel);
    samples += cycles;
  }
  return PhaseRun{.samples = samples, .level = level};
}

std::int16_t timecents(double seconds) {
  if (seconds <= 0.0) {
    return MinTimecents;
  }
  const double value = std::round(1200.0 * std::log2(seconds));
  return static_cast<std::int16_t>(std::clamp(value, double{MinTimecents}, double{MaxTimecents}));
}

double secondsOf(const PhaseRun& run) {
  return static_cast<double>(run.samples) / SampleRate;
}

// The time of a 100 dB fall at the rate a falling run from `from` showed.
std::int16_t fallTimecents(const PhaseRun& run, std::int32_t from) {
  if (run.level <= 0) {
    return timecents(secondsOf(run));
  }
  const double decibels = 20.0 * std::log10(static_cast<double>(from) / run.level);
  if (decibels <= 0.0) {
    return MinTimecents;
  }
  return timecents(secondsOf(run) * FullFallDecibels / decibels);
}

// The time of a falling phase from a level: scaled to 100 dB when exponential, to silence when
// linear.
std::int16_t fallTime(const Phase& phase, std::int32_t from) {
  if (phase.exponential) {
    return fallTimecents(runPhase(phase, from, std::max(1, from / MeasureFallRatio)), from);
  }
  return timecents(secondsOf(runPhase(phase, from, 0)));
}

} // namespace

SpuAdsr decodeAdsr(std::uint16_t adsr1, std::uint16_t adsr2) {
  const auto bits = [](std::uint16_t value, unsigned shift, unsigned mask) {
    return static_cast<std::uint8_t>((value >> shift) & mask);
  };
  return SpuAdsr{.attackExponential = (adsr1 & 0x8000U) != 0,
                 .attackShift = bits(adsr1, 10, 0x1F),
                 .attackStep = bits(adsr1, 8, 0x03),
                 .decayShift = bits(adsr1, 4, 0x0F),
                 .sustainLevel = bits(adsr1, 0, 0x0F),
                 .sustainExponential = (adsr2 & 0x8000U) != 0,
                 .sustainDecrease = (adsr2 & 0x4000U) != 0,
                 .sustainShift = bits(adsr2, 8, 0x1F),
                 .sustainStep = bits(adsr2, 6, 0x03),
                 .releaseExponential = (adsr2 & 0x0020U) != 0,
                 .releaseShift = bits(adsr2, 0, 0x1F)};
}

VolumeEnvelope approximateEnvelope(std::uint16_t adsr1, std::uint16_t adsr2) {
  const SpuAdsr adsr = decodeAdsr(adsr1, adsr2);
  VolumeEnvelope envelope;

  const Phase attack{.shift = adsr.attackShift,
                     .step = RiseBase - adsr.attackStep,
                     .exponential = adsr.attackExponential,
                     .decreasing = false};
  envelope.attack = timecents(secondsOf(runPhase(attack, 0, MaxLevel)));

  const std::int32_t sustainLevel = std::min((adsr.sustainLevel + 1) * SustainLevelUnit, MaxLevel);
  envelope.sustain = static_cast<std::int16_t>(
      std::clamp(std::round(200.0 * std::log10(static_cast<double>(MaxLevel) / sustainLevel)), 0.0,
                 double{MaxCentibels}));
  const Phase decay{
      .shift = adsr.decayShift, .step = FallBase, .exponential = true, .decreasing = true};
  envelope.decay = sustainLevel >= MaxLevel ? MinTimecents : fallTime(decay, MaxLevel);

  const auto sustainRate = static_cast<std::uint8_t>((adsr.sustainShift << 2U) | adsr.sustainStep);
  envelope.slopedSustain = sustainRate != FrozenSustainRate;
  if (envelope.slopedSustain && adsr.sustainDecrease) {
    const Phase sustain{.shift = adsr.sustainShift,
                        .step = FallBase + adsr.sustainStep,
                        .exponential = adsr.sustainExponential,
                        .decreasing = true};
    envelope.decay = fallTime(sustain, sustainLevel);
    envelope.sustain = SilenceCentibels;
  }

  const Phase release{.shift = adsr.releaseShift,
                      .step = FallBase,
                      .exponential = adsr.releaseExponential,
                      .decreasing = true};
  envelope.release = fallTime(release, MaxLevel);
  return envelope;
}

} // namespace dwe::formats::audio
