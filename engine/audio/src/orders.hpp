#pragma once

#include <dwe/audio/clip.hpp>
#include <dwe/audio/mixing.hpp>
#include <dwe/audio/voice.hpp>

#include <optional>
#include <variant>

#include "spsc_queue.hpp"

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
};

struct StopOrder {
  VoiceId voice;
};

struct VolumeOrder {
  VolumeControl control = VolumeControl::Master;
  float volume = 1.0F;
};

using Order = std::variant<PlayOrder, StopOrder, VolumeOrder>;

// A voice ended: it played out, was stopped or was replaced.
struct VoiceEnded {
  VoiceId voice;
};

using OrderQueue = SpscQueue<Order, CommandQueueCapacity>;
// Every voice id ends exactly once, and the mixer takes an order only while the end it may cause
// fits, so a full queue delays orders and never loses an end. The game empties this queue at every
// update. Between two updates the ends that can arrive are those of the voices playing at the first
// (at most MaxVoices) and of the voices started by the orders it published (at most one full order
// queue): MaxVoices + CommandQueueCapacity. Twice that keeps an update that comes late from
// delaying orders.
using EndQueue = SpscQueue<VoiceEnded, 2 * (MaxVoices + CommandQueueCapacity)>;

} // namespace dwe::audio
