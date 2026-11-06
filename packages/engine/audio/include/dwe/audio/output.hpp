#pragma once

#include <dwe/audio/mixing.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>

namespace dwe::audio {

// Whatever fills the output with sound: interleaved stereo float frames at MixSampleRate.
class MixSource {
public:
  MixSource() = default;
  MixSource(const MixSource&) = delete;
  MixSource& operator=(const MixSource&) = delete;
  MixSource(MixSource&&) = delete;
  MixSource& operator=(MixSource&&) = delete;
  virtual ~MixSource() = default;

  // Writes frames.size() / OutputChannels frames. Runs on the audio thread when there is one.
  virtual void render(std::span<float> frames) noexcept = 0;
};

// Where the mix goes: the sound device, or nowhere.
class Output {
public:
  Output() = default;
  Output(const Output&) = delete;
  Output& operator=(const Output&) = delete;
  Output(Output&&) = delete;
  Output& operator=(Output&&) = delete;
  virtual ~Output() = default;

  // Starts pulling sound from source, which must outlive the output.
  virtual void start(MixSource& source) = 0;
  // Called once per game frame with the time that passed. An output with its own thread ignores it.
  virtual void advance(double seconds) = 0;
};

// An output without a device, for tests, headless runs and machines without sound: each advance
// renders the frames that time is worth on the calling thread and discards them, so voices still
// play, end and report it, deterministically.
class NullOutput final : public Output {
public:
  void start(MixSource& source) override;
  void advance(double seconds) override;

  [[nodiscard]] std::uint64_t renderedFrames() const { return renderedFrames_; }

private:
  MixSource* source_ = nullptr;
  double pendingFrames_ = 0.0;
  std::array<float, MaxRenderFrames * OutputChannels> discarded_{};
  std::uint64_t renderedFrames_ = 0;
};

// How long openDeviceOutput waits for the sound device by default.
inline constexpr std::chrono::milliseconds DeviceOpenTimeout{3000};

// Opens the system's default playback device, paused until start. Fails, with SDL's reason, when
// there is no device or no sound server. The device opens on a thread of its own: a sound server
// that never answers (a hung PipeWire) fails it after `timeout` instead of blocking the game, and
// that thread is left behind (deviceOpenStuck).
core::Result<std::unique_ptr<Output>>
openDeviceOutput(std::chrono::milliseconds timeout = DeviceOpenTimeout);

// Whether a device open timed out and its thread is still stuck in SDL's audio start. SDL_Quit
// would then wait for it, so the app ends the process without shutting SDL down.
[[nodiscard]] bool deviceOpenStuck();

} // namespace dwe::audio
