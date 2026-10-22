#include "game_audio.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <memory>
#include <utility>

#include "messages.hpp"

namespace dwe::app {

audio::Audio openGameAudio() {
  auto device = audio::openDeviceOutput();
  if (!device) {
    core::logWarning("{}", core::formatMessage(messages::AudioSilent, device.error().message));
    return audio::Audio{std::make_unique<audio::NullOutput>()};
  }
  return audio::Audio{std::move(*device)};
}

} // namespace dwe::app
