#include <dwe/audio/clip.hpp>
#include <dwe/core/message.hpp>

#include "messages.hpp"

namespace dwe::audio {

std::uint64_t Clip::frameCount() const {
  return channels == 0 ? 0 : pcm.size() / channels;
}

core::Result<void> checkClip(const Clip& clip) {
  if (clip.channels != 1 && clip.channels != 2) {
    return core::fail(core::formatMessage(messages::ClipChannels, clip.channels));
  }
  if (clip.sampleRate == 0) {
    return core::fail(std::string{messages::ClipNoRate});
  }
  if (clip.pcm.size() % clip.channels != 0) {
    return core::fail(
        core::formatMessage(messages::ClipPartialFrame, clip.channels, clip.pcm.size()));
  }
  if (clip.frameCount() == 0) {
    return core::fail(std::string{messages::ClipEmpty});
  }
  if (const auto& loop = clip.loop;
      loop && (loop->length == 0 || loop->start + loop->length > clip.frameCount())) {
    return core::fail(core::formatMessage(messages::ClipLoopOutside, loop->length, loop->start,
                                          clip.frameCount()));
  }
  return {};
}

} // namespace dwe::audio
