#include <dwe/assets/media_headers.hpp>
#include <dwe/audio/mixing.hpp>
#include <dwe/audio/sound_font.hpp>

#include <tsf.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

#include "messages.hpp"

namespace dwe::audio {

namespace {

// The font's bytes as the synthesizer's stream (tsf.h: struct tsf_stream). tsf_load_memory casts
// callbacks that take its own memory type to the stream's types, and UBSan's function check reports
// every read through them; these callbacks have the stream's exact types.
struct FontStream {
  std::span<const std::byte> bytes;
  std::size_t position = 0;
};

int readFont(void* data, void* destination, unsigned int size) {
  auto& stream = *static_cast<FontStream*>(data);
  const std::size_t count = std::min<std::size_t>(size, stream.bytes.size() - stream.position);
  std::ranges::copy(stream.bytes.subspan(stream.position, count),
                    static_cast<std::byte*>(destination));
  stream.position += count;
  return static_cast<int>(count);
}

int skipFont(void* data, unsigned int count) {
  auto& stream = *static_cast<FontStream*>(data);
  if (count > stream.bytes.size() - stream.position) {
    return 0;
  }
  stream.position += count;
  return 1;
}

} // namespace

core::Result<std::shared_ptr<SoundFont>> SoundFont::load(std::span<const std::byte> sf2) {
  if (sf2.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return core::fail(std::string{messages::FontTooBig});
  }
  // The synthesizer trusts the chunk sizes it reads, so the file's layout is checked first.
  if (auto header = assets::readSoundFontHeader(sf2); !header) {
    return std::unexpected{header.error()};
  }
  FontStream bytes{.bytes = sf2, .position = 0};
  tsf_stream stream{.data = &bytes, .read = &readFont, .skip = &skipFont};
  tsf* synth = tsf_load(&stream);
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
