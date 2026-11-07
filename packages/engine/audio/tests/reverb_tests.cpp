#include <dwe/audio/audio.hpp>
#include <dwe/audio/mixing.hpp>
#include <dwe/audio/output.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "chip_reverb.hpp"
#include "clip_fixtures.hpp"
#include "mixer.hpp"
#include "orders.hpp"

using namespace dwe::audio;

namespace {

constexpr std::size_t SecondFrames = MixSampleRate;
// The shortest way through the preset, from a reflection's write to a comb's read: 0xDD units of 8
// bytes, 884 chip ticks, about 40 ms.
constexpr double FirstEchoSeconds = 884.0 / ChipReverb::SampleRate;

double energy(std::span<const float> samples) {
  double sum = 0.0;
  for (const float sample : samples) {
    sum += static_cast<double>(sample) * sample;
  }
  return sum;
}

std::size_t frames(double seconds) {
  return static_cast<std::size_t>(seconds * MixSampleRate);
}

// A click on the left side, then silence: what the reverb makes of it, frame by frame.
std::vector<float> impulseResponse(std::size_t frameCount, std::size_t channel = 0) {
  const auto reverb = std::make_unique<ChipReverb>();
  std::vector<float> send(frameCount * OutputChannels, 0.0F);
  send[channel] = 1.0F;
  std::vector<float> out(send.size(), 0.0F);
  reverb->process(send, out);
  return out;
}

std::span<const float> secondsOf(std::span<const float> samples, double from, double to) {
  return samples.subspan(frames(from) * OutputChannels,
                         (frames(to) - frames(from)) * OutputChannels);
}

// A mixer with its two queues, standing in for both threads.
struct MixerRig {
  OrderQueue orders;
  EndQueue ends;
  VoicePositions positions{};
  std::atomic<std::uint64_t> musicTaken{0};
  Mixer mixer{orders, ends, positions, musicTaken};

  std::vector<float> render(std::size_t frameCount) {
    std::vector<float> samples(frameCount * OutputChannels);
    mixer.render(samples);
    return samples;
  }
};

} // namespace

TEST_CASE("silence through the chip's reverb stays silence") {
  const auto reverb = std::make_unique<ChipReverb>();
  std::vector<float> send(SecondFrames * OutputChannels, 0.0F);
  std::vector<float> out(send.size(), 0.0F);
  reverb->process(send, out);
  CHECK(std::ranges::all_of(out, [](float sample) { return sample == 0.0F; }));
}

TEST_CASE("a click comes back from the reverb after the preset's first echo, on both sides") {
  const auto out = impulseResponse(frames(1.0));
  // Nothing before the shortest path through the buffer.
  CHECK(energy(secondsOf(out, 0.0, FirstEchoSeconds * 0.9)) == 0.0);
  // Then a tail on the same side and, through the cross reflections, on the other.
  const auto tail = secondsOf(out, FirstEchoSeconds, 0.5);
  double left = 0.0;
  double right = 0.0;
  for (std::size_t i = 0; i < tail.size(); i += OutputChannels) {
    left += static_cast<double>(tail[i]) * tail[i];
    right += static_cast<double>(tail[i + 1]) * tail[i + 1];
  }
  CHECK(left > 0.0);
  CHECK(right > 0.0);
}

TEST_CASE("the reverb's tail fades out to the chip's last bits") {
  const auto out = impulseResponse(frames(12.0));
  const double early = energy(secondsOf(out, 0.0, 1.0));
  const double late = energy(secondsOf(out, 3.0, 4.0));
  CHECK(early > 0.0);
  CHECK(late < early / 100.0);
  // The chip stores 16-bit samples, so the tail ends in silence or in a cycle of its last bits,
  // never in a float tail that decays forever.
  const auto end = secondsOf(out, 11.0, 12.0);
  CHECK(std::ranges::all_of(end, [](float sample) { return std::abs(sample) <= 2.0F / 32768.0F; }));
}

TEST_CASE("a cleared reverb forgets its tail") {
  const auto reverb = std::make_unique<ChipReverb>();
  std::vector<float> send(frames(0.02) * OutputChannels, 0.0F);
  send[0] = 1.0F;
  std::vector<float> out(send.size(), 0.0F);
  reverb->process(send, out);
  reverb->clear();
  std::vector<float> silence(frames(1.0) * OutputChannels, 0.0F);
  std::vector<float> after(silence.size(), 0.0F);
  reverb->process(silence, after);
  CHECK(energy(after) == 0.0);
}

TEST_CASE("only a voice with a reverb send leaves a tail after it ends") {
  // A 10 ms click that plays out long before the mix is read.
  const auto click = tests::steadyClip(16384, frames(0.01));
  const auto tailOf = [&](float reverbSend, bool reverbOn) {
    auto rig = std::make_unique<MixerRig>();
    if (!reverbOn) {
      REQUIRE(rig->orders.push(ReverbOrder{.enabled = false}));
    }
    REQUIRE(rig->orders.push(PlayOrder{.voice = {.slot = 0, .generation = 1},
                                       .clip = click.get(),
                                       .bus = Bus::Effects,
                                       .reverbSend = reverbSend}));
    const auto out = rig->render(frames(0.5));
    return energy(secondsOf(out, 0.1, 0.5));
  };
  CHECK(tailOf(1.0F, true) > 0.0);
  CHECK(tailOf(0.0F, true) == 0.0);
  CHECK(tailOf(1.0F, false) == 0.0);
}

TEST_CASE("the reverb starts on, as the original sets it at boot, and can be turned off") {
  Audio audio{std::make_unique<NullOutput>()};
  CHECK(audio.reverb());
  audio.setReverb(false);
  CHECK_FALSE(audio.reverb());
  audio.setReverb(true);
  CHECK(audio.reverb());
}
