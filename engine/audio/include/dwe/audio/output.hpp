#pragma once

#include <dwe/audio/mixing.hpp>
#include <dwe/core/error.hpp>

#include <array>
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
  virtual void render(std::span<float> frames) = 0;
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

  // The frames rendered since the start.
  [[nodiscard]] std::uint64_t renderedFrames() const { return renderedFrames_; }

private:
  MixSource* source_ = nullptr;
  double pendingFrames_ = 0.0;
  std::array<float, MaxRenderFrames * OutputChannels> discarded_{};
  std::uint64_t renderedFrames_ = 0;
};

// Opens the system's default playback device, paused until start. Fails, with SDL's reason, when
// there is no device or no sound server.
core::Result<std::unique_ptr<Output>> openDeviceOutput();

} // namespace dwe::audio
