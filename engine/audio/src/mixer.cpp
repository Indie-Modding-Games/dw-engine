#include "mixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <variant>

namespace dwe::audio {

namespace {

constexpr float VolumeStep =
    static_cast<float>(1.0 / (VolumeRampSeconds * static_cast<double>(MixSampleRate)));

float rampToward(float current, float target) {
  if (current < target) {
    return std::min(current + VolumeStep, target);
  }
  return std::max(current - VolumeStep, target);
}

constexpr std::size_t busVolume(Bus bus) {
  return bus == Bus::Music ? std::to_underlying(VolumeControl::Music)
                           : std::to_underlying(VolumeControl::Effects);
}

} // namespace

Mixer::Mixer(OrderQueue& orders, EndQueue& ends) : orders_{orders}, ends_{ends} {}

void Mixer::render(std::span<float> frames) noexcept {
  applyOrders();
  constexpr std::size_t PassSamples = MaxRenderFrames * OutputChannels;
  while (!frames.empty()) {
    const std::size_t samples = std::min(frames.size(), PassSamples);
    renderPass(frames.first(samples));
    frames = frames.subspan(samples);
  }
}

// An order may end the voice it replaces, so it waits while that end would not fit: a voice's end
// is never lost and its slot is never taken before the game hears of it.
void Mixer::applyOrders() noexcept {
  while (ends_.hasRoom()) {
    const auto order = orders_.pop();
    if (!order) {
      return;
    }
    std::visit([this](const auto& known) { apply(known); }, *order);
  }
}

void Mixer::apply(const PlayOrder& order) noexcept {
  VoicePlayer& voice = voices_[order.voice.slot];
  if (voice.state() != VoicePlayer::State::Idle) {
    reportEnd(voice);
  }
  voice.start(order);
}

void Mixer::apply(const StopOrder& order) noexcept {
  VoicePlayer& voice = voices_[order.voice.slot];
  if (voice.state() == VoicePlayer::State::Playing && voice.voice() == order.voice) {
    voice.stop();
  }
}

void Mixer::apply(const VolumeOrder& order) noexcept {
  volumes_[std::to_underlying(order.control)].target = order.volume;
}

void Mixer::renderPass(std::span<float> frames) {
  std::ranges::fill(frames, 0.0F);
  const std::size_t frameCount = frames.size() / OutputChannels;
  rampBusGains(frameCount);
  for (VoicePlayer& voice : voices_) {
    if (voice.state() == VoicePlayer::State::Playing) {
      const auto& gains = busGains_[std::to_underlying(voice.bus())];
      voice.mixInto(frames, std::span{gains}.first(frameCount));
    }
    if (voice.state() == VoicePlayer::State::Ended) {
      reportEnd(voice);
    }
  }
  // The original's sound chip saturates at 16 bits, so a hard clip is the faithful limit. A NaN
  // that got this far becomes silence instead of reaching the device.
  for (float& sample : frames) {
    sample = std::isnan(sample) ? 0.0F : std::clamp(sample, -1.0F, 1.0F);
  }
}

void Mixer::rampBusGains(std::size_t frameCount) {
  Ramp& master = volumes_[std::to_underlying(VolumeControl::Master)];
  for (std::size_t i = 0; i < frameCount; ++i) {
    for (Ramp& volume : volumes_) {
      volume.current = rampToward(volume.current, volume.target);
    }
    for (const Bus bus : {Bus::Music, Bus::Effects}) {
      busGains_[std::to_underlying(bus)][i] = master.current * volumes_[busVolume(bus)].current;
    }
  }
}

// A full queue keeps the voice ended, and the next pass tries again.
void Mixer::reportEnd(VoicePlayer& voice) {
  if (ends_.push(VoiceEnded{voice.voice()})) {
    voice.release();
  }
}

} // namespace dwe::audio
