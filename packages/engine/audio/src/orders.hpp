#pragma once

#include <dwe/assets/music.hpp>
#include <dwe/audio/clip.hpp>
#include <dwe/audio/mixing.hpp>
#include <dwe/audio/voice.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <variant>

#include "spsc_queue.hpp"

struct tsf;

// What the game thread tells the audio thread, and what it hears back.
namespace dwe::audio {

// Starts a voice in its slot, replacing whatever the slot held. The game thread keeps the clip
// alive until the voice's end comes back.
struct PlayOrder {
  VoiceId voice;
  const Clip* clip = nullptr;
  Bus bus = Bus::Effects;
  float gain = 1.0F;
  float pan = 0.0F;
  float pitch = 1.0F;
  std::optional<ChipEnvelope> envelope = std::nullopt;
  float reverbSend = 0.0F;
};

struct StopOrder {
  VoiceId voice;
};

struct VolumeOrder {
  VolumeControl control = VolumeControl::Master;
  float volume = 1.0F;
};

// Replaces the music, a hard cut. The game thread keeps the font and the song alive until the mixer
// has taken a later music order (Mixer::musicTaken).
struct MusicOrder {
  std::uint64_t serial = 0;
  tsf* synth = nullptr;
  const assets::Song* song = nullptr;
  bool loops = true;
  float gain = 1.0F;
};

// Cuts the music.
struct MusicStopOrder {
  std::uint64_t serial = 0;
};

// Turns the sound chip's reverb on or off; off, its tail is dropped at once.
struct ReverbOrder {
  bool enabled = true;
};

using Order =
    std::variant<PlayOrder, StopOrder, VolumeOrder, MusicOrder, MusicStopOrder, ReverbOrder>;

// A voice ended: it played out, was stopped or was replaced.
struct VoiceEnded {
  VoiceId voice;
};

using OrderQueue = SpscQueue<Order, CommandQueueCapacity>;

// Where each slot's voice is in its clip, as the audio thread last mixed it: the voice's
// generation in the high 32 bits and the clip frame in the low 32 (packVoicePosition). The game
// reads it to follow a voice, for instance a movie's sound as its clock.
using VoicePositions = std::array<std::atomic<std::uint64_t>, MaxVoices>;

[[nodiscard]] constexpr std::uint64_t packVoicePosition(std::uint32_t generation,
                                                        std::uint64_t frame) {
  constexpr std::uint64_t FrameMask = 0xFFFF'FFFFU;
  return (std::uint64_t{generation} << 32U) | std::min(frame, FrameMask);
}
// Every voice id ends exactly once, and the mixer takes an order only while the end it may cause
// fits, so a full queue delays orders and never loses an end. The game empties this queue at every
// update. Between two updates the ends that can arrive are those of the voices playing at the first
// (at most MaxVoices) and of the voices started by the orders it published (at most one full order
// queue): MaxVoices + CommandQueueCapacity. Twice that keeps an update that comes late from
// delaying orders.
using EndQueue = SpscQueue<VoiceEnded, 2 * (MaxVoices + CommandQueueCapacity)>;

} // namespace dwe::audio
