#include <dwe/audio/mixing.hpp>
#include <dwe/audio/output.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>

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

// What the thread that opens the device hands back. The thread owns it with the caller, which may
// stop waiting for it.
struct DeviceOpening {
  std::mutex mutex;
  std::condition_variable done;
  bool finished = false;
  // The caller stopped waiting: whatever the thread opens now stays unused, and SDL_Quit frees it.
  bool abandoned = false;
  SDL_AudioStream* stream = nullptr;
  std::string error;
};

// Set while a device open the caller gave up on has not returned.
std::atomic<bool> openStuck{false};

void openOn(DeviceOpening& opening) {
  SDL_AudioStream* stream = nullptr;
  std::string error;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    error = SDL_GetError();
  } else {
    const SDL_AudioSpec spec{.format = SDL_AUDIO_F32,
                             .channels = static_cast<int>(OutputChannels),
                             .freq = static_cast<int>(MixSampleRate)};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
      error = SDL_GetError();
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
  }
  // SDL keeps per-thread data (its error text) for threads it did not make until told.
  SDL_CleanupTLS();
  const std::scoped_lock lock{opening.mutex};
  opening.stream = stream;
  opening.error = std::move(error);
  opening.finished = true;
  if (opening.abandoned) {
    // Too late: the game went on silent. The paused stream is left to SDL_Quit, which can now
    // shut the audio down normally.
    openStuck = false;
  }
  opening.done.notify_all();
}

} // namespace

core::Result<std::unique_ptr<Output>> openDeviceOutput(std::chrono::milliseconds timeout) {
  auto opening = std::make_shared<DeviceOpening>();
  const auto started = std::chrono::steady_clock::now();
  std::thread thread{[opening, started] {
    openOn(*opening);
    const std::scoped_lock lock{opening->mutex};
    if (opening->abandoned) {
      const auto late = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started);
      core::logWarning("audio: {}", core::formatMessage(messages::DeviceOpenedLate, late.count()));
    }
  }};
  std::unique_lock lock{opening->mutex};
  if (!opening->done.wait_for(lock, timeout, [&opening] { return opening->finished; })) {
    // A sound server that never answers keeps SDL's audio start waiting forever, and nothing can
    // cancel it: the thread is left behind and the game plays silent.
    opening->abandoned = true;
    openStuck = true;
    lock.unlock();
    thread.detach();
    return core::fail(core::formatMessage(messages::DeviceTimedOut, timeout.count()));
  }
  lock.unlock();
  thread.join();
  if (opening->stream == nullptr) {
    return core::fail(core::formatMessage(messages::DeviceUnavailable, opening->error));
  }
  return std::make_unique<SdlOutput>(opening->stream);
}

bool deviceOpenStuck() {
  return openStuck;
}

} // namespace dwe::audio
