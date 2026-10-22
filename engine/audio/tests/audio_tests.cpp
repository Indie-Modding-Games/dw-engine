#include <dwe/audio/audio.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "clip_fixtures.hpp"

using namespace dwe::audio;
using namespace dwe::audio::tests;

namespace {

constexpr double Tick = 1.0 / 60.0;

// An Audio on the null output, and a way to look at that output.
struct NullAudio {
  NullOutput* output = nullptr;
  Audio audio;

  NullAudio() : NullAudio{std::make_unique<NullOutput>()} {}

private:
  explicit NullAudio(std::unique_ptr<NullOutput> made)
      : output{made.get()}, audio{std::move(made)} {}
};

std::shared_ptr<const Clip> loopingClip() {
  return steadyClip(1000, 64, ClipLoop{.start = 0, .length = 64});
}

} // namespace

TEST_CASE("the null output renders the frames the game's time is worth") {
  NullAudio rig;
  for (int tick = 0; tick < 60; ++tick) {
    rig.audio.update(Tick);
  }
  CHECK(rig.output->renderedFrames() == MixSampleRate);
  rig.audio.update(0.5 / MixSampleRate);
  rig.audio.update(0.5 / MixSampleRate);
  CHECK(rig.output->renderedFrames() == MixSampleRate + 1);
}

TEST_CASE("a voice plays from the next update and frees its slot when its end comes back") {
  NullAudio rig;
  const auto voice = rig.audio.play({.clip = steadyClip(1000, 100)});
  REQUIRE(voice);
  CHECK(rig.audio.isActive(*voice));
  rig.audio.update(Tick); // sent and played out
  CHECK(rig.audio.isActive(*voice));
  rig.audio.update(Tick); // its end is collected
  CHECK_FALSE(rig.audio.isActive(*voice));
  CHECK(rig.audio.activeVoiceCount() == 0);
}

TEST_CASE("a stopped voice ends after its fade") {
  NullAudio rig;
  const auto voice = rig.audio.play({.clip = loopingClip()});
  REQUIRE(voice);
  rig.audio.update(Tick);
  rig.audio.update(Tick);
  CHECK(rig.audio.isActive(*voice));
  rig.audio.stop(*voice);
  rig.audio.update(Tick);
  rig.audio.update(Tick);
  CHECK_FALSE(rig.audio.isActive(*voice));
}

TEST_CASE("past the voice limit a new voice replaces the oldest one") {
  NullAudio rig;
  std::vector<VoiceId> voices;
  for (std::size_t i = 0; i < MaxVoices; ++i) {
    const auto voice = rig.audio.play({.clip = loopingClip()});
    REQUIRE(voice);
    voices.push_back(*voice);
  }
  rig.audio.update(Tick);
  CHECK(rig.audio.activeVoiceCount() == MaxVoices);

  const auto newest = rig.audio.play({.clip = loopingClip()});
  REQUIRE(newest);
  CHECK(newest->slot == voices.front().slot);
  CHECK_FALSE(rig.audio.isActive(voices.front()));
  CHECK(rig.audio.isActive(voices[1]));
  rig.audio.update(Tick);
  rig.audio.update(Tick);
  CHECK(rig.audio.isActive(*newest));
  CHECK(rig.audio.activeVoiceCount() == MaxVoices);

  const auto next = rig.audio.play({.clip = loopingClip()});
  REQUIRE(next);
  CHECK(next->slot == voices[1].slot);
}

TEST_CASE("one frame's orders are capped and the next frame takes orders again") {
  NullAudio rig;
  for (std::size_t i = 0; i < CommandQueueCapacity; ++i) {
    REQUIRE(rig.audio.play({.clip = steadyClip(1000, 10)}));
  }
  const auto refused = rig.audio.play({.clip = steadyClip(1000, 10)});
  REQUIRE_FALSE(refused);
  CHECK(refused.error().message.contains("full"));
  rig.audio.update(Tick);
  CHECK(rig.audio.play({.clip = steadyClip(1000, 10)}));
}

TEST_CASE("a voice needs a playable clip and a positive pitch") {
  NullAudio rig;
  CHECK_FALSE(rig.audio.play({}));
  CHECK_FALSE(rig.audio.play({.clip = clipOf({}, 1)}));
  CHECK_FALSE(rig.audio.play({.clip = steadyClip(1000, 4), .pitch = 0.0F}));
  CHECK_FALSE(rig.audio.play(
      {.clip = steadyClip(1000, 4), .pitch = std::numeric_limits<float>::infinity()}));
  CHECK(rig.audio.activeVoiceCount() == 0);
}

TEST_CASE("volumes start whole and are kept between 0 and 1") {
  NullAudio rig;
  CHECK(rig.audio.volume(VolumeControl::Master) == 1.0F);
  rig.audio.setVolume(VolumeControl::Music, 0.25F);
  rig.audio.setVolume(VolumeControl::Effects, 3.0F);
  rig.audio.setVolume(VolumeControl::Master, std::nanf(""));
  CHECK(rig.audio.volume(VolumeControl::Music) == 0.25F);
  CHECK(rig.audio.volume(VolumeControl::Effects) == 1.0F);
  CHECK(rig.audio.volume(VolumeControl::Master) == 0.0F);
}

TEST_CASE("an Audio keeps working after a move") {
  NullAudio rig;
  Audio moved = std::move(rig.audio);
  const auto voice = moved.play({.clip = steadyClip(1000, 10)});
  REQUIRE(voice);
  moved.update(Tick);
  moved.update(Tick);
  CHECK_FALSE(moved.isActive(*voice));
}
