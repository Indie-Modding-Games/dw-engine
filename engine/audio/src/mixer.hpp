#pragma once

#include <dwe/audio/mixing.hpp>
#include <dwe/audio/output.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "music_player.hpp"
#include "orders.hpp"
#include "voice_player.hpp"

namespace dwe::audio {

// The audio thread's side: applies the game's orders at the start of each pass, mixes the voices
// into the buses and the buses into the output, and reports the voices that end. It never
// allocates, locks or logs.
class Mixer final : public MixSource {
public:
  // musicTaken receives the serial of each music order once applied: the music of every earlier
  // order is no longer played, so the game thread may free it.
  Mixer(OrderQueue& orders, EndQueue& ends, VoicePositions& positions,
        std::atomic<std::uint64_t>& musicTaken);

  void render(std::span<float> frames) noexcept override;

private:
  // The volumes as they sound now and where they are going.
  struct Ramp {
    float current = 1.0F;
    float target = 1.0F;
  };

  void applyOrders() noexcept;
  void apply(const PlayOrder& order) noexcept;
  void apply(const StopOrder& order) noexcept;
  void apply(const VolumeOrder& order) noexcept;
  void apply(const MusicOrder& order) noexcept;
  void apply(const MusicStopOrder& order) noexcept;
  void renderPass(std::span<float> frames);
  // The gain of each bus for each frame of the pass, master included.
  void rampBusGains(std::size_t frameCount);
  void reportEnd(VoicePlayer& voice);

  OrderQueue& orders_;
  EndQueue& ends_;
  VoicePositions& positions_;
  std::atomic<std::uint64_t>& musicTaken_;
  MusicPlayer music_;
  std::array<VoicePlayer, MaxVoices> voices_{};
  std::array<Ramp, VolumeControlCount> volumes_{};
  std::array<std::array<float, MaxRenderFrames>, BusCount> busGains_{};
};

} // namespace dwe::audio
