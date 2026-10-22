#include "voice_player.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace dwe::audio {

namespace {

constexpr float SampleScale = 1.0F / 32768.0F;
constexpr float FadeStep =
    static_cast<float>(1.0 / (StopFadeSeconds * static_cast<double>(MixSampleRate)));

} // namespace

StereoGains panGains(float pan, std::uint16_t channels) {
  const float clamped = std::clamp(pan, -1.0F, 1.0F);
  if (channels == 2) {
    return {.left = std::min(1.0F, 1.0F - clamped), .right = std::min(1.0F, 1.0F + clamped)};
  }
  const float angle = (clamped + 1.0F) * std::numbers::pi_v<float> / 4.0F;
  return {.left = std::cos(angle), .right = std::sin(angle)};
}

void VoicePlayer::start(const PlayOrder& order) {
  state_ = State::Playing;
  voice_ = order.voice;
  clip_ = order.clip;
  bus_ = order.bus;
  const StereoGains pan = panGains(order.pan, clip_->channels);
  gains_ = {.left = pan.left * order.gain, .right = pan.right * order.gain};
  position_ = 0.0;
  step_ = static_cast<double>(clip_->sampleRate) / static_cast<double>(MixSampleRate) *
          static_cast<double>(order.pitch);
  stopping_ = false;
  fade_ = 1.0F;
}

void VoicePlayer::stop() {
  stopping_ = true;
}

void VoicePlayer::release() {
  state_ = State::Idle;
  clip_ = nullptr;
}

void VoicePlayer::mixInto(std::span<float> frames, std::span<const float> busGains) noexcept {
  const std::size_t frameCount = frames.size() / OutputChannels;
  const auto lastChannel = static_cast<std::uint16_t>(clip_->channels - 1);
  for (std::size_t i = 0; i < frameCount && state_ == State::Playing; ++i) {
    const auto frame = static_cast<std::uint64_t>(position_);
    const auto fraction = static_cast<float>(position_ - static_cast<double>(frame));
    const std::uint64_t next = nextFrame(frame);
    const auto interpolate = [&](std::uint16_t channel) {
      const float from = sampleAt(frame, channel);
      return from + ((sampleAt(next, channel) - from) * fraction);
    };
    const float gain = busGains[i] * fade_;
    frames[i * OutputChannels] += interpolate(0) * gains_.left * gain;
    frames[(i * OutputChannels) + 1] += interpolate(lastChannel) * gains_.right * gain;
    advance();
  }
}

float VoicePlayer::sampleAt(std::uint64_t frame, std::uint16_t channel) const {
  if (frame >= clip_->frameCount()) {
    return 0.0F;
  }
  return static_cast<float>(clip_->pcm[(frame * clip_->channels) + channel]) * SampleScale;
}

std::uint64_t VoicePlayer::nextFrame(std::uint64_t frame) const {
  if (clip_->loop && frame + 1 == clip_->loop->start + clip_->loop->length) {
    return clip_->loop->start;
  }
  return frame + 1;
}

void VoicePlayer::advance() {
  position_ += step_;
  if (const auto& loop = clip_->loop) {
    const auto loopEnd = static_cast<double>(loop->start + loop->length);
    if (position_ >= loopEnd) {
      const auto loopStart = static_cast<double>(loop->start);
      position_ = loopStart + std::fmod(position_ - loopStart, static_cast<double>(loop->length));
      // Rounding can land exactly on the end when the loop starts far into the clip.
      if (position_ >= loopEnd) {
        position_ = loopStart;
      }
    }
  } else if (position_ >= static_cast<double>(clip_->frameCount())) {
    state_ = State::Ended;
  }
  if (stopping_) {
    fade_ -= FadeStep;
    if (fade_ <= 0.0F) {
      state_ = State::Ended;
    }
  }
}

} // namespace dwe::audio
