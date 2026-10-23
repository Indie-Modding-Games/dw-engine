#include <dwe/assets/media_headers.hpp>
#include <dwe/audio/mixing.hpp>
#include <dwe/audio/sound_font.hpp>

#include <tsf.h>

#include <limits>
#include <string>
#include <utility>

#include "messages.hpp"

namespace dwe::audio {

core::Result<std::shared_ptr<SoundFont>> SoundFont::load(std::span<const std::byte> sf2) {
  if (sf2.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return core::fail(std::string{messages::FontTooBig});
  }
  // The synthesizer trusts the chunk sizes it reads, so the file's layout is checked first.
  if (auto header = assets::readSoundFontHeader(sf2); !header) {
    return std::unexpected{header.error()};
  }
  tsf* synth = tsf_load_memory(sf2.data(), static_cast<int>(sf2.size()));
  if (synth == nullptr) {
    return core::fail(std::string{messages::FontUnreadable});
  }
  std::shared_ptr<SoundFont> font{new SoundFont{synth}};
  tsf_set_output(synth, TSF_STEREO_INTERLEAVED, static_cast<int>(MixSampleRate), 0.0F);
  // The voices and the channels are allocated here, so the audio thread never allocates: with a
  // voice limit the synthesizer steals a voice instead of growing, and a channel is set up the
  // first time it is touched.
  if (tsf_set_max_voices(synth, MaxMusicVoices) == 0 ||
      tsf_channel_set_pitchwheel(synth, MusicChannels - 1, 8192) == 0) {
    return core::fail(std::string{messages::FontNoMemory});
  }
  return font;
}

SoundFont::~SoundFont() {
  tsf_close(synth_);
}

} // namespace dwe::audio
