#pragma once

#include <dwe/assets/music.hpp>
#include <dwe/audio/mixing.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "orders.hpp"

namespace dwe::audio {

// The music on the audio thread: a sequencer that walks a song's events by audio frames and the
// SoundFont synthesizer that sounds them (docs/architecture/audio.md, "Música (tramo 3)"). It never
// allocates, locks or logs.
class MusicPlayer {
public:
  // A hard cut to a new song, from its start, with every channel reset.
  void start(const MusicOrder& order) noexcept;
  // A hard cut to silence.
  void stop() noexcept;
  // Adds the music to the frames, each scaled by its gain (the Music bus).
  void mixInto(std::span<float> frames, std::span<const float> gains) noexcept;

private:
  // The synthesizer renders in blocks this long, so the events land within this many frames.
  static constexpr std::size_t BlockFrames = 64;

  void resetChannels() noexcept;
  void sequence() noexcept;
  void dispatch(const assets::SongEvent& event) noexcept;
  void setTempo(std::uint32_t microsecondsPerQuarter) noexcept;

  tsf* synth_ = nullptr;
  const assets::Song* song_ = nullptr;
  bool loops_ = true;
  bool sequencing_ = false;
  float gain_ = 1.0F;
  std::size_t next_ = 0;
  double tick_ = 0.0;
  double ticksPerFrame_ = 0.0;
  std::uint32_t loopStart_ = 0;
  std::uint32_t loopEnd_ = 0;
  std::array<float, BlockFrames * OutputChannels> block_{};
};

} // namespace dwe::audio
