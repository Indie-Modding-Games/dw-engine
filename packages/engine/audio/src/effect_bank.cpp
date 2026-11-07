#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/sound.hpp>
#include <dwe/audio/effect_bank.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>

#include "messages.hpp"

namespace dwe::audio {

namespace {

// The chip plays a sample at its own rate when the key is the tone's root, in steps of a 16th of a
// semitone, and its pitch register tops out at 0x3FFF / 0x1000 times that rate.
constexpr double ChipRate = 44100.0;
constexpr double MaxChipRate = ChipRate * 0x3FFF / 0x1000;
constexpr int StepsPerSemitone = 16;
constexpr double StepsPerOctave = 12.0 * StepsPerSemitone;
// The fine tune is in 128ths of a semitone and the chip keeps its top 4 bits.
constexpr int FineTuneToSteps = 3;
constexpr float MidiMax = 127.0F;
constexpr int PanCentre = 64;

core::Result<std::vector<std::byte>> readAsset(const assets::VirtualFileSystem& files,
                                               const std::string& id) {
  auto parsed = assets::AssetId::parse(id);
  if (!parsed) {
    return std::unexpected{parsed.error()};
  }
  return files.read(*parsed);
}

core::Result<EffectSample> loadSample(const assets::VirtualFileSystem& files,
                                      std::string_view folder,
                                      const assets::SoundBankSample& sample) {
  const std::string_view file = sample.file;
  const std::string id = std::format("{}/{}", folder, file.substr(0, file.rfind('.')));
  auto parsed = assets::AssetId::parse(id);
  if (!parsed) {
    return std::unexpected{parsed.error()};
  }
  const assets::AssetSource* source = files.resolve(*parsed);
  if (source == nullptr) {
    return core::fail(core::formatMessage(messages::BankSampleMissing, id));
  }
  auto bytes = files.read(*parsed);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  auto sound =
      source->file.extension() == ".wav" ? assets::decodeWav(*bytes) : assets::decodeOpus(*bytes);
  if (!sound) {
    return core::fail(core::formatMessage(messages::BankSampleBad, id, sound.error().message));
  }
  auto clip = std::make_shared<Clip>(Clip{.sampleRate = sound->sampleRate,
                                          .channels = sound->channels,
                                          .pcm = std::move(sound->pcm),
                                          .loop = std::nullopt});
  if (sound->loop) {
    clip->loop = ClipLoop{.start = sound->loop->start, .length = sound->loop->length};
  }
  if (auto playable = checkClip(*clip); !playable) {
    return core::fail(core::formatMessage(messages::BankSampleBad, id, playable.error().message));
  }
  if (sound->sourceRate != 0 && sound->sourceRate != sample.rate) {
    core::logWarning(
        "{}", core::formatMessage(messages::BankSampleRate, id, sound->sourceRate, sample.rate));
  }
  return EffectSample{.clip = std::move(clip), .sourceRate = sound->sourceRate};
}

float level(std::uint8_t volume) {
  return static_cast<float>(volume) / MidiMax;
}

} // namespace

EffectBank::EffectBank(assets::SoundBank bank, std::vector<EffectSample> samples)
    : bank_(std::move(bank)), samples_(std::move(samples)) {}

core::Result<EffectBank> EffectBank::load(const assets::VirtualFileSystem& files,
                                          std::string_view folder) {
  auto meta = readAsset(files, std::format("{}/bank_meta", folder));
  if (!meta) {
    return std::unexpected{meta.error()};
  }
  const std::string text(reinterpret_cast<const char*>(meta->data()), meta->size());
  auto bank = assets::readSoundBank(text);
  if (!bank) {
    return std::unexpected{bank.error()};
  }
  std::vector<EffectSample> samples;
  samples.reserve(bank->samples.size());
  for (const assets::SoundBankSample& sample : bank->samples) {
    auto loaded = loadSample(files, folder, sample);
    if (!loaded) {
      return std::unexpected{loaded.error()};
    }
    samples.push_back(std::move(*loaded));
  }
  return EffectBank{std::move(*bank), std::move(samples)};
}

core::Result<std::vector<PlayRequest>> EffectBank::requests(std::uint32_t value) const {
  const assets::SoundBankSound* sound = bank_.sound(value);
  if (sound == nullptr) {
    return core::fail(core::formatMessage(messages::BankNoSound, value));
  }
  // readSoundBank checks these, but a bank may be built by hand.
  const assets::SoundBankProgram* program = bank_.program(sound->program);
  if (program == nullptr) {
    return core::fail(core::formatMessage(messages::BankBrokenSound, value));
  }
  std::vector<PlayRequest> voices;
  for (const std::uint32_t number : sound->tones) {
    const auto tone = std::ranges::find(program->tones, number, &assets::SoundBankTone::tone);
    if (tone == program->tones.end()) {
      return core::fail(core::formatMessage(messages::BankBrokenSound, value));
    }
    const auto sample =
        std::ranges::find(bank_.samples, tone->sample, &assets::SoundBankSample::sample);
    const auto index = static_cast<std::size_t>(sample - bank_.samples.begin());
    if (sample == bank_.samples.end() || index >= samples_.size()) {
      return core::fail(core::formatMessage(messages::BankBrokenSound, value));
    }
    // As the original's note-to-pitch routine does
    // (docs/architecture/audio.md#64-el-tramo-2-en-detalle).
    const int steps =
        ((sound->note - tone->root) * StepsPerSemitone) + (tone->fineTune >> FineTuneToSteps);
    const double unclamped = ChipRate * std::exp2(steps / StepsPerOctave);
    const double rate = std::min(unclamped, MaxChipRate);
    const EffectSample& loaded = samples_[index];
    const std::uint32_t sourceRate = loaded.sourceRate != 0 ? loaded.sourceRate : sample->rate;
    core::logDebug("[port:audio] note2pitch2 (libsnd.s:8717): sound={} tone={} note={} root={} "
                   "steps16={} rate={:.1f}{} pitch={:.4f}",
                   value, number, sound->note, tone->root, steps, rate,
                   unclamped > MaxChipRate ? " (clamped)" : "",
                   rate / static_cast<double>(sourceRate));
    const int pan =
        std::clamp((tone->pan - PanCentre) + (program->pan - PanCentre), -PanCentre, PanCentre);
    voices.push_back({.clip = loaded.clip,
                      .bus = Bus::Effects,
                      .gain = level(tone->volume) * level(program->volume) * level(bank_.volume),
                      .pan = static_cast<float>(pan) / static_cast<float>(PanCentre),
                      .pitch = static_cast<float>(rate / static_cast<double>(sourceRate)),
                      .envelope = ChipEnvelope{.adsr1 = tone->adsr1, .adsr2 = tone->adsr2},
                      .reverbSend = tone->reverb() ? 1.0F : 0.0F});
  }
  return voices;
}

core::Result<std::vector<VoiceId>> EffectBank::play(Audio& audio, std::uint32_t value) const {
  auto voices = requests(value);
  if (!voices) {
    core::logDebug("[port:audio] startSound (sound.c:602): sound={} failed: {}", value,
                   voices.error().message);
    return std::unexpected{voices.error()};
  }
  auto started = audio.playAll(*voices);
  core::logDebug("[port:audio] startSound (sound.c:602): sound={} voices={}{}", value,
                 voices->size(), started ? "" : " failed: " + started.error().message);
  return started;
}

} // namespace dwe::audio
