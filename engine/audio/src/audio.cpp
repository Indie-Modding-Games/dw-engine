#include <dwe/audio/audio.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <ranges>
#include <string>
#include <utility>

#include "messages.hpp"
#include "mixer.hpp"
#include "orders.hpp"

namespace dwe::audio {

// What the two threads share, on the heap so the output's pointer to the mixer survives a move.
struct Audio::Shared {
  OrderQueue orders;
  EndQueue ends;
  Mixer mixer{orders, ends};
};

namespace {

std::uint64_t keyOf(VoiceId voice) {
  return (std::uint64_t{voice.slot} << std::numeric_limits<std::uint32_t>::digits) |
         voice.generation;
}

float clampedVolume(float volume) {
  // Also turns NaN into silence.
  return volume > 0.0F ? std::min(volume, 1.0F) : 0.0F;
}

} // namespace

Audio::Audio(std::unique_ptr<Output> output)
    : shared_{std::make_unique<Shared>()}, output_{std::move(output)}, slots_(MaxVoices) {
  output_->start(shared_->mixer);
}

Audio::Audio(Audio&&) noexcept = default;
Audio& Audio::operator=(Audio&&) noexcept = default;

Audio::~Audio() {
  // The output goes first, so the device stops pulling before the mixer goes.
  output_.reset();
}

core::Result<VoiceId> Audio::play(const PlayRequest& request) {
  if (!request.clip) {
    return core::fail(std::string{messages::PlayNoClip});
  }
  if (auto playable = checkClip(*request.clip); !playable) {
    return std::unexpected{playable.error()};
  }
  if (!(request.pitch > 0.0F) || !std::isfinite(request.pitch)) {
    return core::fail(core::formatMessage(messages::PlayBadPitch, request.pitch));
  }
  if (!shared_->orders.hasRoom()) {
    ++droppedOrders_;
    return core::fail(core::formatMessage(messages::OrdersFull, CommandQueueCapacity));
  }
  const std::uint32_t slot = takeSlot();
  Slot& taken = slots_[slot];
  const VoiceId voice{.slot = slot, .generation = ++taken.generation};
  taken.active = true;
  taken.startOrder = nextStartOrder_++;
  playingClips_.insert_or_assign(keyOf(voice), request.clip);
  shared_->orders.stage(PlayOrder{.voice = voice,
                                  .clip = request.clip.get(),
                                  .bus = request.bus,
                                  .gain = clampedVolume(request.gain),
                                  .pan = std::clamp(request.pan, -1.0F, 1.0F),
                                  .pitch = request.pitch});
  return voice;
}

void Audio::stop(VoiceId voice) {
  if (isActive(voice) && !shared_->orders.stage(StopOrder{.voice = voice})) {
    ++droppedOrders_;
  }
}

void Audio::setVolume(VolumeControl control, float volume) {
  const float clamped = clampedVolume(volume);
  volumes_[std::to_underlying(control)] = clamped;
  if (!shared_->orders.stage(VolumeOrder{.control = control, .volume = clamped})) {
    ++droppedOrders_;
  }
}

float Audio::volume(VolumeControl control) const {
  return volumes_[std::to_underlying(control)];
}

bool Audio::isActive(VoiceId voice) const {
  return voice.slot < slots_.size() && slots_[voice.slot].active &&
         slots_[voice.slot].generation == voice.generation;
}

std::size_t Audio::activeVoiceCount() const {
  return static_cast<std::size_t>(std::ranges::count_if(slots_, &Slot::active));
}

void Audio::update(double seconds) {
  collectEndedVoices();
  shared_->orders.publish();
  logGuards();
  output_->advance(seconds);
}

// A free slot, or the oldest voice's, which the mixer then ends.
std::uint32_t Audio::takeSlot() {
  const auto free = std::ranges::find_if(slots_, [](const Slot& slot) { return !slot.active; });
  if (free != slots_.end()) {
    return static_cast<std::uint32_t>(std::distance(slots_.begin(), free));
  }
  ++stolenVoices_;
  const auto oldest = std::ranges::min_element(slots_, {}, &Slot::startOrder);
  return static_cast<std::uint32_t>(std::distance(slots_.begin(), oldest));
}

void Audio::collectEndedVoices() {
  while (const auto ended = shared_->ends.pop()) {
    playingClips_.erase(keyOf(ended->voice));
    if (isActive(ended->voice)) {
      slots_[ended->voice.slot].active = false;
    }
  }
}

// Once per frame, so a runaway script fills the log with one line a frame, not one per sound.
void Audio::logGuards() {
  if (droppedOrders_ > 0) {
    core::logWarning(
        "{}", core::formatMessage(messages::OrdersDropped, droppedOrders_, CommandQueueCapacity));
    droppedOrders_ = 0;
  }
  if (stolenVoices_ > 0) {
    core::logWarning("{}", core::formatMessage(messages::VoicesStolen, stolenVoices_, MaxVoices));
    stolenVoices_ = 0;
  }
}

} // namespace dwe::audio
