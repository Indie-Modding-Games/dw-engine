#pragma once

#include <dwe/audio/mixing.hpp>
#include <dwe/audio/output.hpp>

#include <array>
#include <cstddef>
#include <span>

#include "orders.hpp"
#include "voice_player.hpp"

namespace dwe::audio {

// The audio thread's side: applies the game's orders at the start of each pass, mixes the voices
// into the buses and the buses into the output, and reports the voices that end. It never
// allocates, locks or logs.
class Mixer final : public MixSource {
public:
  Mixer(OrderQueue& orders, EndQueue& ends);

  void render(std::span<float> frames) override;

private:
  // The volumes as they sound now and where they are going.
  struct Ramp {
    float current = 1.0F;
    float target = 1.0F;
  };

  void applyOrders();
  void apply(const PlayOrder& order);
  void apply(const StopOrder& order);
  void apply(const VolumeOrder& order);
  void renderPass(std::span<float> frames);
  // The gain of each bus for each frame of the pass, master included.
  void rampBusGains(std::size_t frameCount);
  void reportEnd(VoicePlayer& voice);

  OrderQueue& orders_;
  EndQueue& ends_;
  std::array<VoicePlayer, MaxVoices> voices_{};
  std::array<Ramp, VolumeControlCount> volumes_{};
  std::array<std::array<float, MaxRenderFrames>, BusCount> busGains_{};
};

} // namespace dwe::audio
