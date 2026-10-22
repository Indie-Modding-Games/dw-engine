#pragma once

#include <dwe/audio/clip.hpp>
#include <dwe/audio/mixing.hpp>
#include <dwe/audio/voice.hpp>

#include <cstdint>
#include <span>

#include "orders.hpp"

namespace dwe::audio {

// How loud a voice is in each output channel.
struct StereoGains {
  float left = 0.0F;
  float right = 0.0F;
};

// The pan law: constant power for a mono clip (-3 dB per side in the centre), and balance for a
// stereo clip, which keeps both sides whole in the centre.
[[nodiscard]] StereoGains panGains(float pan, std::uint16_t channels);

// One voice on the audio thread: where it is in its clip, how fast it moves and how loud it is.
class VoicePlayer {
public:
  enum class State : std::uint8_t {
    Idle,
    Playing,
    // Done, but the game has not been told yet.
    Ended,
  };

  void start(const PlayOrder& order);
  void stop();
  void release();

  // Adds the voice to frames (interleaved stereo), scaled by its bus's gain at each frame.
  void mixInto(std::span<float> frames, std::span<const float> busGains) noexcept;

  [[nodiscard]] State state() const { return state_; }
  [[nodiscard]] VoiceId voice() const { return voice_; }
  [[nodiscard]] Bus bus() const { return bus_; }

private:
  [[nodiscard]] float sampleAt(std::uint64_t frame, std::uint16_t channel) const;
  [[nodiscard]] std::uint64_t nextFrame(std::uint64_t frame) const;
  void advance();

  State state_ = State::Idle;
  VoiceId voice_;
  const Clip* clip_ = nullptr;
  Bus bus_ = Bus::Effects;
  StereoGains gains_;
  double position_ = 0.0;
  double step_ = 1.0;
  bool stopping_ = false;
  float fade_ = 1.0F;
};

} // namespace dwe::audio
