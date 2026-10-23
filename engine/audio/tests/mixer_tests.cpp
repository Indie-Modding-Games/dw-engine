#include <dwe/audio/mixing.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

#include "clip_fixtures.hpp"
#include "mixer.hpp"
#include "orders.hpp"

using namespace dwe::audio;
using namespace dwe::audio::tests;

namespace {

constexpr float Half = 16384.0F / 32768.0F;
const float centreGain = std::cos(std::numbers::pi_v<float> / 4.0F);
constexpr std::size_t RampFrames = 480;
constexpr std::size_t FadeFrames = 240;

// A mixer with its two queues, standing in for both threads.
struct MixerRig {
  OrderQueue orders;
  EndQueue ends;
  VoicePositions positions{};
  std::atomic<std::uint64_t> musicTaken{0};
  Mixer mixer{orders, ends, positions, musicTaken};

  void send(const Order& order) { REQUIRE(orders.push(order)); }

  void play(VoiceId voice, const Clip& clip, Bus bus = Bus::Effects, float pan = 0.0F,
            float pitch = 1.0F) {
    send(PlayOrder{
        .voice = voice, .clip = &clip, .bus = bus, .gain = 1.0F, .pan = pan, .pitch = pitch});
  }

  std::vector<float> render(std::size_t frames) {
    std::vector<float> samples(frames * OutputChannels);
    mixer.render(samples);
    return samples;
  }

  std::vector<VoiceId> ended() {
    std::vector<VoiceId> voices;
    while (const auto end = ends.pop()) {
      voices.push_back(end->voice);
    }
    return voices;
  }
};

std::unique_ptr<MixerRig> makeRig() {
  return std::make_unique<MixerRig>();
}

float left(std::span<const float> samples, std::size_t frame) {
  return samples[frame * OutputChannels];
}
float right(std::span<const float> samples, std::size_t frame) {
  return samples[(frame * OutputChannels) + 1];
}

} // namespace

TEST_CASE("a mono voice in the centre sounds at -3 dB on each side") {
  auto rig = makeRig();
  const auto clip = steadyClip(16384, 8);
  rig->play({.slot = 0, .generation = 1}, *clip);
  const auto out = rig->render(4);
  CHECK(left(out, 0) == doctest::Approx(Half * centreGain));
  CHECK(right(out, 3) == doctest::Approx(Half * centreGain));
}

TEST_CASE("panning a mono voice keeps its power and moves it to one side") {
  auto rig = makeRig();
  const auto clip = steadyClip(16384, 8);
  rig->play({.slot = 0, .generation = 1}, *clip, Bus::Effects, -1.0F);
  rig->play({.slot = 1, .generation = 1}, *clip, Bus::Effects, 0.5F);
  const auto out = rig->render(1);
  const float angle = 1.5F * std::numbers::pi_v<float> / 4.0F;
  CHECK(left(out, 0) == doctest::Approx(Half + (Half * std::cos(angle))));
  CHECK(right(out, 0) == doctest::Approx(Half * std::sin(angle)));
}

TEST_CASE("a stereo voice keeps both sides whole in the centre and balances off it") {
  auto rig = makeRig();
  const auto clip = clipOf({16384, -8192, 16384, -8192}, 2);
  rig->play({.slot = 0, .generation = 1}, *clip);
  auto out = rig->render(1);
  CHECK(left(out, 0) == doctest::Approx(0.5F));
  CHECK(right(out, 0) == doctest::Approx(-0.25F));

  auto panned = makeRig();
  panned->play({.slot = 0, .generation = 1}, *clip, Bus::Effects, 0.5F);
  out = panned->render(1);
  CHECK(left(out, 0) == doctest::Approx(0.25F));
  CHECK(right(out, 0) == doctest::Approx(-0.25F));
}

TEST_CASE("voices add up and the sum is clipped to full scale") {
  auto rig = makeRig();
  const auto clip = clipOf({16384, 16384, 32767, 32767}, 2);
  rig->play({.slot = 0, .generation = 1}, *clip);
  rig->play({.slot = 1, .generation = 1}, *clip);
  const auto out = rig->render(2);
  CHECK(left(out, 0) == doctest::Approx(1.0F));
  CHECK(right(out, 1) == 1.0F);
}

TEST_CASE("a volume multiplies its bus, the master multiplies both, after a short ramp") {
  auto rig = makeRig();
  const auto clip = clipOf(std::vector<std::int16_t>(4000, 16384), 2);
  rig->play({.slot = 0, .generation = 1}, *clip, Bus::Effects);
  rig->play({.slot = 1, .generation = 1}, *clip, Bus::Music, 1.0F);
  rig->send(VolumeOrder{.control = VolumeControl::Master, .volume = 0.5F});
  rig->send(VolumeOrder{.control = VolumeControl::Effects, .volume = 0.5F});
  const auto out = rig->render(RampFrames + 1);
  // Effects in both sides; the music, panned right, only in the right.
  CHECK(left(out, RampFrames) == doctest::Approx(0.5F * 0.25F));
  CHECK(right(out, RampFrames) == doctest::Approx((0.5F * 0.25F) + (0.5F * 0.5F)));
}

TEST_CASE("a volume change ramps without a jump") {
  auto rig = makeRig();
  const auto clip = clipOf(std::vector<std::int16_t>(4000, 32767), 2);
  rig->play({.slot = 0, .generation = 1}, *clip);
  rig->send(VolumeOrder{.control = VolumeControl::Master, .volume = 0.0F});
  const auto out = rig->render(RampFrames + 10);
  for (std::size_t frame = 1; frame < RampFrames + 10; ++frame) {
    CHECK(left(out, frame - 1) - left(out, frame) <= doctest::Approx(1.0F / RampFrames));
  }
  CHECK(left(out, RampFrames) == 0.0F);
}

TEST_CASE("a voice that plays out ends, falls silent and reports it once") {
  auto rig = makeRig();
  const auto clip = clipOf({16384, 16384, 16384, 16384}, 2);
  const VoiceId voice{.slot = 3, .generation = 7};
  rig->play(voice, *clip);
  const auto out = rig->render(4);
  CHECK(left(out, 1) == doctest::Approx(0.5F));
  CHECK(rig->ended() == std::vector{voice});
  const auto after = rig->render(4);
  CHECK(left(after, 0) == 0.0F);
  CHECK(rig->ended().empty());
}

TEST_CASE("a looping voice goes back to the loop's start and never ends by itself") {
  auto rig = makeRig();
  const auto clip = clipOf({0, 0, 3000, 3000, 6000, 6000, 9000, 9000}, 2, 48000,
                           ClipLoop{.start = 1, .length = 3});
  rig->play({.slot = 0, .generation = 1}, *clip);
  const auto out = rig->render(8);
  const std::vector<std::size_t> expected{0, 1, 2, 3, 1, 2, 3, 1};
  for (std::size_t frame = 0; frame < expected.size(); ++frame) {
    CHECK(left(out, frame) ==
          doctest::Approx(static_cast<float>(expected[frame]) * 3000.0F / 32768.0F));
  }
  CHECK(rig->ended().empty());
}

TEST_CASE("a clip at another rate is resampled with linear interpolation") {
  auto rig = makeRig();
  const auto clip = clipOf({0, 0, 16384, 16384}, 2, 24000);
  rig->play({.slot = 0, .generation = 1}, *clip);
  const auto out = rig->render(3);
  CHECK(left(out, 0) == 0.0F);
  CHECK(left(out, 1) == doctest::Approx(0.25F));
  CHECK(left(out, 2) == doctest::Approx(0.5F));
}

TEST_CASE("pitch changes the speed through the clip") {
  auto rig = makeRig();
  const auto clip = clipOf({0, 0, 8192, 8192, 16384, 16384}, 2);
  rig->play({.slot = 0, .generation = 1}, *clip, Bus::Effects, 0.0F, 2.0F);
  const auto out = rig->render(2);
  CHECK(left(out, 1) == doctest::Approx(0.5F));
}

TEST_CASE("a stopped voice fades out before it ends; a stop for an older voice does nothing") {
  auto rig = makeRig();
  const auto clip = steadyClip(16384, 4, ClipLoop{.start = 0, .length = 4});
  const VoiceId voice{.slot = 0, .generation = 2};
  rig->play(voice, *clip);
  rig->send(StopOrder{.voice = {.slot = 0, .generation = 1}});
  CHECK(rig->render(16)[30] == doctest::Approx(Half * centreGain));
  CHECK(rig->ended().empty());

  rig->send(StopOrder{.voice = voice});
  const auto out = rig->render(FadeFrames + 10);
  CHECK(left(out, FadeFrames / 2) == doctest::Approx(Half * centreGain * 0.5F).epsilon(0.01));
  CHECK(left(out, FadeFrames + 5) == 0.0F);
  CHECK(rig->ended() == std::vector{voice});
}

TEST_CASE("a voice that takes a playing voice's slot ends the old one") {
  auto rig = makeRig();
  const auto clip = steadyClip(16384, 4, ClipLoop{.start = 0, .length = 4});
  const VoiceId old{.slot = 5, .generation = 1};
  const VoiceId replacing{.slot = 5, .generation = 2};
  rig->play(old, *clip);
  rig->render(2);
  rig->play(replacing, *clip);
  const auto out = rig->render(2);
  CHECK(left(out, 0) == doctest::Approx(Half * centreGain));
  CHECK(rig->ended() == std::vector{old});
}

TEST_CASE("a render longer than one pass is mixed in pieces without a seam") {
  auto rig = makeRig();
  const auto clip = steadyClip(16384, MaxRenderFrames * 3);
  rig->play({.slot = 0, .generation = 1}, *clip);
  const auto out = rig->render((MaxRenderFrames * 2) + 7);
  CHECK(left(out, MaxRenderFrames - 1) == doctest::Approx(Half * centreGain));
  CHECK(left(out, MaxRenderFrames) == doctest::Approx(Half * centreGain));
  CHECK(left(out, (MaxRenderFrames * 2) + 6) == doctest::Approx(Half * centreGain));
}

TEST_CASE("a NaN that reaches the mix becomes silence, not noise for every voice") {
  auto rig = makeRig();
  const auto clip = steadyClip(16384, 8);
  rig->send(
      PlayOrder{.voice = {.slot = 0, .generation = 1}, .clip = clip.get(), .pan = std::nanf("")});
  rig->play({.slot = 1, .generation = 1}, *clip);
  const auto out = rig->render(2);
  CHECK(left(out, 0) == 0.0F);
  CHECK(right(out, 1) == 0.0F);
}

TEST_CASE("an end that does not fit is reported on a later pass") {
  auto rig = makeRig();
  const auto clip = steadyClip(16384, 4);
  const VoiceId voice{.slot = 2, .generation = 1};
  rig->play(voice, *clip);
  rig->render(1);
  std::size_t filler = 0;
  while (rig->ends.push(VoiceEnded{.voice = {.slot = 9, .generation = 9}})) {
    ++filler;
  }
  rig->render(8);
  REQUIRE(rig->ended().size() == filler);
  rig->render(1);
  CHECK(rig->ended() == std::vector{voice});
}

TEST_CASE("orders wait while the end they may cause would not fit") {
  auto rig = makeRig();
  while (rig->ends.push(VoiceEnded{})) {
  }
  const auto clip = steadyClip(16384, 8);
  rig->play({.slot = 0, .generation = 1}, *clip);
  CHECK(left(rig->render(1), 0) == 0.0F);
  rig->ended();
  CHECK(left(rig->render(1), 0) == doctest::Approx(Half * centreGain));
}

TEST_CASE("staged items stay out of sight until they are published") {
  auto queue = std::make_unique<EndQueue>();
  REQUIRE(queue->stage(VoiceEnded{.voice = {.slot = 1, .generation = 1}}));
  REQUIRE(queue->stage(VoiceEnded{.voice = {.slot = 2, .generation = 1}}));
  CHECK_FALSE(queue->pop());
  queue->publish();
  CHECK(queue->pop().value_or(VoiceEnded{}).voice.slot == 1);
  CHECK(queue->pop().value_or(VoiceEnded{}).voice.slot == 2);
  CHECK_FALSE(queue->pop());
}

TEST_CASE("a looping voice whose envelope fades out ends by itself") {
  auto rig = makeRig();
  const auto clip = steadyClip(16384, 64, ClipLoop{.start = 0, .length = 64});
  // Instant attack, full sustain level, then a fast linear fall in the sustain.
  rig->send(PlayOrder{.voice = {.slot = 0, .generation = 1},
                      .clip = clip.get(),
                      .bus = Bus::Effects,
                      .gain = 1.0F,
                      .pan = 0.0F,
                      .pitch = 1.0F,
                      .envelope = ChipEnvelope{.adsr1 = 0x000F, .adsr2 = 0x4000}});
  const auto out = rig->render(2);
  CHECK(left(out, 1) > 0.0F);
  rig->render(480);
  CHECK(rig->ended() == std::vector<VoiceId>{{.slot = 0, .generation = 1}});
}
