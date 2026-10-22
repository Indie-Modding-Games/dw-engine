#include <dwe/formats/audio/envelope.hpp>
#include <dwe/formats/audio/vab_sf2.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <utility>

namespace dwe::formats::audio {

namespace {

constexpr double FullVolume = 127.0;
constexpr double MaxAttenuation = 1440.0;
constexpr int PanCentre = 64;
constexpr int PanRange = 500;
constexpr std::int16_t ReverbOn = 1000;
constexpr std::uint32_t SoundFontRate = 44100;
constexpr std::uint8_t MaxKey = 127;
constexpr std::int16_t LoopContinuously = 1;

using EnvelopeCache = std::map<std::pair<std::uint16_t, std::uint16_t>, VolumeEnvelope>;

const VolumeEnvelope& envelopeOf(EnvelopeCache& cache, const ToneAttributes& tone) {
  const auto key = std::pair{tone.adsr1, tone.adsr2};
  auto found = cache.find(key);
  if (found == cache.end()) {
    found = cache.emplace(key, approximateEnvelope(tone.adsr1, tone.adsr2)).first;
  }
  return found->second;
}

Sf2Zone zoneOf(const Vab& vab, std::size_t program, const ToneAttributes& tone,
               const VolumeEnvelope& envelope, const DecodedSample& sample,
               std::size_t sampleIndex) {
  const auto& programAttributes = vab.programs[program];
  std::vector<Sf2GeneratorValue> generators = {
      {.generator = Sf2Generator::InitialAttenuation,
       .amount = toneAttenuation(tone.vol, programAttributes.mvol, vab.header.mvol)},
      {.generator = Sf2Generator::Pan, .amount = tonePan(tone.pan, programAttributes.mpan)},
      {.generator = Sf2Generator::ReverbEffectsSend,
       .amount = (tone.mode & ToneAttributes::ReverbMode) != 0 ? ReverbOn : std::int16_t{0}},
      {.generator = Sf2Generator::AttackVolEnv, .amount = envelope.attack},
      {.generator = Sf2Generator::DecayVolEnv, .amount = envelope.decay},
      {.generator = Sf2Generator::SustainVolEnv, .amount = envelope.sustain},
      {.generator = Sf2Generator::ReleaseVolEnv, .amount = envelope.release},
      {.generator = Sf2Generator::FineTune, .amount = toneFineTune(tone.shift)},
      {.generator = Sf2Generator::OverridingRootKey, .amount = std::min(tone.center, MaxKey)},
  };
  if (sample.loop) {
    generators.push_back({.generator = Sf2Generator::SampleModes, .amount = LoopContinuously});
  }
  const std::uint8_t keyHigh = std::min(tone.max, MaxKey);
  return Sf2Zone{.keyLow = std::min(tone.min, keyHigh),
                 .keyHigh = keyHigh,
                 .generators = std::move(generators),
                 .sample = sampleIndex};
}

} // namespace

std::int16_t toneAttenuation(std::uint8_t toneVolume, std::uint8_t programVolume,
                             std::uint8_t bankVolume) {
  const double ratio =
      (toneVolume / FullVolume) * (programVolume / FullVolume) * (bankVolume / FullVolume);
  if (ratio <= 0.0) {
    return static_cast<std::int16_t>(MaxAttenuation);
  }
  return static_cast<std::int16_t>(
      std::clamp(std::round(-200.0 * std::log10(ratio)), 0.0, MaxAttenuation));
}

std::int16_t tonePan(std::uint8_t tonePan, std::uint8_t programPan) {
  const int offset =
      std::clamp((tonePan - PanCentre) + (programPan - PanCentre), -PanCentre, PanCentre);
  return static_cast<std::int16_t>(offset * PanRange / PanCentre);
}

std::int16_t toneFineTune(std::uint8_t shift) {
  return static_cast<std::int16_t>(-std::lround(shift * 100.0 / 128.0));
}

core::Result<VabSoundFont> convertVabToSoundFont(const Vab& vab,
                                                 std::span<const DecodedSample> samples,
                                                 std::string_view name) {
  VabSoundFont result;
  result.font.name = std::string{name};
  std::map<std::size_t, std::size_t> sampleIndices;
  EnvelopeCache envelopes;
  for (const auto& block : vab.toneBlocks) {
    Sf2Instrument instrument{.name = std::format("program_{:03}", block.program), .zones = {}};
    for (const auto& tone : vab.tonesOf(block.program)) {
      const auto number = static_cast<std::size_t>(tone.vag);
      if (number >= samples.size() || samples[number].pcm.empty()) {
        return core::fail(std::format("VAB program {} plays sample {}, which has not been decoded",
                                      block.program, number));
      }
      const DecodedSample& sample = samples[number];
      const auto [place, added] = sampleIndices.try_emplace(number, result.font.samples.size());
      if (added) {
        result.font.samples.push_back(Sf2Sample{.name = std::format("vag_{:03}", number),
                                                .pcm = sample.pcm,
                                                .sampleRate = SoundFontRate,
                                                .loop = sample.loop,
                                                .originalPitch = std::min(tone.center, MaxKey),
                                                .pitchCorrection = 0});
      }
      const VolumeEnvelope& envelope = envelopeOf(envelopes, tone);
      if (envelope.slopedSustain) {
        ++result.slopedSustains;
      }
      instrument.zones.push_back(zoneOf(vab, block.program, tone, envelope, sample, place->second));
    }
    result.font.presets.push_back(Sf2Preset{.name = instrument.name,
                                            .preset = static_cast<std::uint16_t>(block.program),
                                            .bank = 0,
                                            .instrument = result.font.instruments.size()});
    result.font.instruments.push_back(std::move(instrument));
  }
  return result;
}

} // namespace dwe::formats::audio
