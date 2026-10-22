#include <dwe/audio/mixing.hpp>
#include <dwe/audio/output.hpp>
#include <dwe/core/message.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>

#include "messages.hpp"

namespace dwe::audio {

namespace {

constexpr std::size_t FrameBytes = sizeof(float) * OutputChannels;

// The default playback device through an SDL audio stream: SDL converts the mix to the device's
// format and follows the default device when it changes.
class SdlOutput final : public Output {
public:
  explicit SdlOutput(SDL_AudioStream* stream) : stream_{stream} {}
  SdlOutput(const SdlOutput&) = delete;
  SdlOutput& operator=(const SdlOutput&) = delete;
  SdlOutput(SdlOutput&&) = delete;
  SdlOutput& operator=(SdlOutput&&) = delete;
  // Destroying the stream waits for a callback in progress, so the source is never used after.
  ~SdlOutput() override {
    SDL_DestroyAudioStream(stream_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }

  // The stream was opened paused: SDL's own lock orders this write before the first callback.
  void start(MixSource& source) override {
    source_ = &source;
    SDL_SetAudioStreamGetCallback(stream_, &SdlOutput::feed, this);
    SDL_ResumeAudioStreamDevice(stream_);
  }

  void advance(double /*seconds*/) override {}

private:
  // SDL's audio thread asks for more bytes.
  static void SDLCALL feed(void* output, SDL_AudioStream* stream, int additionalBytes,
                           int /*totalBytes*/) {
    static_cast<SdlOutput*>(output)->fill(stream, additionalBytes);
  }

  void fill(SDL_AudioStream* stream, int bytes) {
    auto frames = static_cast<std::size_t>(std::max(bytes, 0)) / FrameBytes;
    while (frames > 0) {
      const std::size_t pass = std::min(frames, MaxRenderFrames);
      const std::span samples = std::span{buffer_}.first(pass * OutputChannels);
      source_->render(samples);
      SDL_PutAudioStreamData(stream, samples.data(), static_cast<int>(samples.size_bytes()));
      frames -= pass;
    }
  }

  SDL_AudioStream* stream_;
  MixSource* source_ = nullptr;
  std::array<float, MaxRenderFrames * OutputChannels> buffer_{};
};

} // namespace

core::Result<std::unique_ptr<Output>> openDeviceOutput() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    return core::fail(core::formatMessage(messages::DeviceUnavailable, SDL_GetError()));
  }
  const SDL_AudioSpec spec{.format = SDL_AUDIO_F32,
                           .channels = static_cast<int>(OutputChannels),
                           .freq = static_cast<int>(MixSampleRate)};
  SDL_AudioStream* stream =
      SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (stream == nullptr) {
    auto failure = core::fail(core::formatMessage(messages::DeviceUnavailable, SDL_GetError()));
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return failure;
  }
  return std::make_unique<SdlOutput>(stream);
}

} // namespace dwe::audio
