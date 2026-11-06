#pragma once

#include <dwe/assets/sound_bank.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/audio/clip.hpp>
#include <dwe/audio/voice.hpp>
#include <dwe/core/error.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace dwe::audio {

// One of a bank's samples, loaded.
struct EffectSample {
  std::shared_ptr<const Clip> clip;
  // The rate the sample was recorded at, which sets its pitch; 0 takes the rate its bank names.
  std::uint32_t sourceRate = 0;
};

// A bank of sound effects with its samples loaded: plays an effect the way the original's sound
// chip did, one voice per tone (docs/architecture/audio.md#64-el-tramo-2-en-detalle).
class EffectBank {
public:
  // `samples` holds each of the bank's samples, in the order of bank.samples.
  EffectBank(assets::SoundBank bank, std::vector<EffectSample> samples);

  // Loads the bank in a folder of the packs, such as sound/ss: its bank_meta and every sample it
  // names, as a WAV (from a dev pack) or an Opus (from a cooked one).
  static core::Result<EffectBank> load(const assets::VirtualFileSystem& files,
                                       std::string_view folder);

  // The voices of an effect: each tone at its gain, pan, pitch and envelope.
  [[nodiscard]] core::Result<std::vector<PlayRequest>> requests(std::uint32_t value) const;

  // Starts every voice of an effect, or none when any of them would fail.
  core::Result<std::vector<VoiceId>> play(Audio& audio, std::uint32_t value) const;

private:
  assets::SoundBank bank_;
  std::vector<EffectSample> samples_;
};

} // namespace dwe::audio
