#include <dwe/assets/sound.hpp>
#include <dwe/formats/audio/wav.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string_view>
#include <vector>

#include "audio_cooker.hpp"

using namespace dwe::dwx;
using dwe::formats::audio::SampleLoop;
using dwe::formats::audio::WavSound;
using dwe::formats::audio::writeWav;

namespace {

std::vector<std::int16_t> sine(std::size_t frames, std::uint32_t rate, double frequency) {
    std::vector<std::int16_t> pcm(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        pcm[i] = static_cast<std::int16_t>(
            10000.0 * std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(i) / rate));
    }
    return pcm;
}

double rms(const std::vector<std::int16_t>& pcm) {
    double sum = 0;
    for (const std::int16_t sample : pcm) {
        sum += static_cast<double>(sample) * sample;
    }
    return std::sqrt(sum / static_cast<double>(pcm.size()));
}

std::size_t zeroCrossings(const std::vector<std::int16_t>& pcm) {
    std::size_t crossings = 0;
    for (std::size_t i = 1; i < pcm.size(); ++i) {
        crossings += (pcm[i - 1] < 0) != (pcm[i] < 0) ? 1 : 0;
    }
    return crossings;
}

} // namespace

TEST_CASE("a WAV cooks to Ogg Opus that the engine decodes, with its loop at 48 kHz") {
    const std::vector<std::int16_t> pcm = sine(22050, 22050, 440.0);
    const auto wav = writeWav(
        WavSound{.pcm = pcm, .sampleRate = 22050, .loop = SampleLoop{.start = 1000, .end = 15000}, .unityNote = 48});
    const auto cooked = cookAudio(wav, CookOptions{});
    REQUIRE(cooked.has_value());
    CHECK(std::string_view{reinterpret_cast<const char*>(cooked->data()), 4} == "OggS");
    CHECK(cooked->size() < wav.size());
    // Deterministic: the same WAV gives the same bytes.
    const auto again = cookAudio(wav, CookOptions{});
    REQUIRE(again.has_value());
    CHECK(*again == *cooked);

    const auto sound = dwe::assets::decodeOpus(*cooked);
    REQUIRE(sound.has_value());
    CHECK(sound->sampleRate == 48000);
    CHECK(sound->channels == 1);
    CHECK(sound->sourceRate == 22050);
    CHECK(sound->unityNote == 48);
    REQUIRE(sound->pcm.size() == 48000);
    CHECK(sound->loop == dwe::assets::SoundLoop{.start = 2177, .length = 32653 - 2177});
    // A lossy codec: the same loudness and pitch, not the same samples.
    CHECK(std::abs(rms(sound->pcm) - rms(pcm)) < rms(pcm) * 0.1);
    CHECK(zeroCrossings(sound->pcm) >= 870);
    CHECK(zeroCrossings(sound->pcm) <= 890);
}

TEST_CASE("loops land on the nearest 48 kHz frame inside the decoded sound") {
    CHECK(opusLoopOf(0, 100, 100, 48000) == OpusLoop{.start = 0, .length = 100});
    CHECK(opusLoopOf(1, 3, 10, 5513) == OpusLoop{.start = 9, .length = 17});
    // 10 frames at 5513 Hz decode to ceil(87.07) = 88 frames.
    CHECK(opusLoopOf(9, 10, 10, 5513) == OpusLoop{.start = 78, .length = 9});
    CHECK(opusLoopOf(20, 30, 10, 5513) == OpusLoop{.start = 88, .length = 0});
}

TEST_CASE("cooking audio refuses what is not a 16-bit PCM WAV") {
    const std::vector<std::byte> text{std::byte{'n'}, std::byte{'o'}};
    const auto notWav = cookAudio(text, CookOptions{});
    REQUIRE_FALSE(notWav.has_value());
    CHECK(notWav.error().message == "not a RIFF WAVE file");
    const auto empty =
        cookAudio(writeWav(WavSound{.pcm = {}, .sampleRate = 11025, .loop = {}, .unityNote = 60}), CookOptions{});
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().message == "the WAV has no samples");
}
