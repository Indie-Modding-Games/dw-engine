#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <memory>
#include <span>

// The synthesizer's own type (tsf.h).
struct tsf;

namespace dwe::audio {

// A SoundFont loaded into the music synthesizer, set to the mixer's rate and voice guard. Once
// handed to Audio::playMusic, only the audio thread plays it; the game thread keeps it alive and
// frees it when the mixer has let it go.
class SoundFont {
public:
  // Reads an SF2 file; the bytes may go once it returns.
  static core::Result<std::shared_ptr<SoundFont>> load(std::span<const std::byte> sf2);

  SoundFont(const SoundFont&) = delete;
  SoundFont& operator=(const SoundFont&) = delete;
  SoundFont(SoundFont&&) = delete;
  SoundFont& operator=(SoundFont&&) = delete;
  ~SoundFont();

  // For the audio thread.
  [[nodiscard]] tsf* synth() const { return synth_; }

private:
  explicit SoundFont(tsf* synth) : synth_{synth} {}

  tsf* synth_;
};

} // namespace dwe::audio
