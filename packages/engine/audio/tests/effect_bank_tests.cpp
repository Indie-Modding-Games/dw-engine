#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/audio/effect_bank.hpp>
#include <dwe/audio/output.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "clip_fixtures.hpp"

using namespace dwe::audio;
using dwe::assets::SoundBank;

namespace {

// Program 0 at pan 96 with two tones: the first at its root, the second half a semitone up by its
// fine tune and panned hard left; tone 2 sits far above every note.
SoundBank twoToneBank() {
  SoundBank bank;
  bank.volume = 127;
  bank.programs.push_back({.program = 0,
                           .volume = 127,
                           .pan = 96,
                           .tones = {{.tone = 0,
                                      .root = 60,
                                      .fineTune = 0,
                                      .volume = 127,
                                      .pan = 64,
                                      .sample = 1,
                                      .adsr1 = 0x80FF,
                                      .adsr2 = 0x5FD5},
                                     {.tone = 1,
                                      .root = 60,
                                      .fineTune = 64,
                                      .volume = 64,
                                      .pan = 0,
                                      .sample = 2,
                                      .adsr1 = 0,
                                      .adsr2 = 0,
                                      .mode = 4},
                                     {.tone = 2,
                                      .root = 127,
                                      .fineTune = 0,
                                      .volume = 127,
                                      .pan = 64,
                                      .sample = 2,
                                      .adsr1 = 0,
                                      .adsr2 = 0}}});
  bank.samples = {{.sample = 1, .file = "sample_001.wav", .rate = 44100},
                  {.sample = 2, .file = "sample_002.wav", .rate = 22050}};
  bank.sounds = {{.value = 0, .program = 0, .note = 60, .tones = {0}},
                 {.value = 5, .program = 0, .note = 72, .tones = {0, 1}},
                 {.value = 6, .program = 0, .note = 127, .tones = {0}},
                 {.value = 7, .program = 0, .note = 0, .tones = {0, 2}}};
  return bank;
}

std::vector<std::byte> monoWav(std::uint32_t rate, std::size_t frames) {
  std::vector<std::byte> bytes;
  const auto put = [&](std::uint32_t value, int size) {
    for (int i = 0; i < size; ++i) {
      bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
    }
  };
  const auto tag = [&](const char* text) {
    for (int i = 0; i < 4; ++i) {
      bytes.push_back(static_cast<std::byte>(text[i]));
    }
  };
  const auto dataSize = static_cast<std::uint32_t>(frames * 2);
  tag("RIFF");
  put(4 + 24 + 8 + dataSize, 4);
  tag("WAVE");
  tag("fmt ");
  put(16, 4);
  put(1, 2);
  put(1, 2);
  put(rate, 4);
  put(rate * 2, 4);
  put(2, 2);
  put(16, 2);
  tag("data");
  put(dataSize, 4);
  for (std::size_t i = 0; i < frames; ++i) {
    put(1000, 2);
  }
  return bytes;
}

constexpr const char* BankMeta = R"(vab: {master_volume: 127}
programs:
- program: 0
  master_volume: 127
  pan: 64
  tones:
  - {tone: 0, center: 60, shift: 0, volume: 127, pan: 64, sample: 1, adsr: "80ff 5fd5"}
samples:
- {sample: 1, file: "sample_001.wav", rate: 11025}
sounds:
- {value: 2, program: 0, note: 60, tones: [0]}
)";

} // namespace

TEST_CASE("an effect's voices take the tone's gain, pan, pitch and envelope") {
  const auto clip = tests::steadyClip(1000, 16);
  const EffectBank bank{twoToneBank(), {{.clip = clip}, {.clip = clip}}};

  const auto one = bank.requests(0);
  REQUIRE(one.has_value());
  REQUIRE(one->size() == 1);
  CHECK((*one)[0].gain == doctest::Approx(1.0F));
  CHECK((*one)[0].pan == doctest::Approx(0.5F));
  CHECK((*one)[0].pitch == doctest::Approx(1.0F));
  REQUIRE((*one)[0].envelope.has_value());
  CHECK((*one)[0].envelope.value_or(ChipEnvelope{}).adsr1 == 0x80FF);
  CHECK((*one)[0].envelope.value_or(ChipEnvelope{}).adsr2 == 0x5FD5);

  const auto two = bank.requests(5);
  REQUIRE(two.has_value());
  REQUIRE(two->size() == 2);
  // An octave up from the root of a sample recorded at the chip's rate.
  CHECK((*two)[0].pitch == doctest::Approx(2.0F));
  // An octave and half a semitone up, from a sample recorded at half the chip's rate.
  CHECK((*two)[1].pitch == doctest::Approx(4.0 * std::exp2(0.5 / 12.0)));
  CHECK((*two)[1].gain == doctest::Approx(64.0F / 127.0F));
  // -64 for the tone and +32 for the program.
  CHECK((*two)[1].pan == doctest::Approx(-0.5F));
  // Only the tone with the chip's reverb bit goes to the reverb.
  CHECK((*two)[0].reverbSend == doctest::Approx(0.0F));
  CHECK((*two)[1].reverbSend == doctest::Approx(1.0F));

  // Five octaves up is past the chip's pitch register, which stops just under two octaves.
  const auto high = bank.requests(6);
  REQUIRE(high.has_value());
  CHECK((*high)[0].pitch == doctest::Approx(0x3FFF / 4096.0));

  CHECK_FALSE(bank.requests(1).has_value());

  // A bank built by hand without the clips of its samples plays nothing.
  const EffectBank clipless{twoToneBank(), {}};
  CHECK_FALSE(clipless.requests(0).has_value());
}

TEST_CASE("an effect plays one voice per tone") {
  const auto clip = tests::steadyClip(1000, 16);
  const EffectBank bank{twoToneBank(), {{.clip = clip}, {.clip = clip}}};
  Audio audio{std::make_unique<NullOutput>()};
  const auto voices = bank.play(audio, 5);
  REQUIRE(voices.has_value());
  CHECK(voices->size() == 2);
  CHECK(audio.activeVoiceCount() == 2);
}

TEST_CASE("an effect with a voice that cannot play starts none of its voices") {
  const auto clip = tests::steadyClip(1000, 16);
  const EffectBank bank{twoToneBank(), {{.clip = clip}, {.clip = clip}}};
  Audio audio{std::make_unique<NullOutput>()};
  // Its second tone is ten octaves down, under the lowest pitch a voice takes.
  CHECK_FALSE(bank.play(audio, 7).has_value());
  CHECK(audio.activeVoiceCount() == 0);

  // With room for one more order this tick, the two voices of an effect start neither.
  for (std::size_t order = 0; order + 1 < CommandQueueCapacity; ++order) {
    audio.setVolume(VolumeControl::Master, 1.0F);
  }
  CHECK_FALSE(bank.play(audio, 5).has_value());
  CHECK(audio.activeVoiceCount() == 0);
  audio.update(1.0 / 60.0);
  CHECK(bank.play(audio, 5).has_value());
}

TEST_CASE("a sample's pitch follows the rate it was encoded at over the one its bank names") {
  const auto clip = tests::steadyClip(1000, 16);
  const EffectBank bank{twoToneBank(), {{.clip = clip}, {.clip = clip, .sourceRate = 44100}}};
  // Sample 2 says 22050 but holds 44100 Hz, so its root plays at the chip's rate.
  const auto voices = bank.requests(5);
  REQUIRE(voices.has_value());
  CHECK((*voices)[1].pitch == doctest::Approx(2.0 * std::exp2(0.5 / 12.0)));
}

TEST_CASE("a bank loads from its folder of the packs, and a missing sample fails it") {
  dwe::assets::MemoryFileSystem memory;
  memory.addFile("pack/pack.yaml", "id: test\nversion: 1.0.0\nlayer: mods\n");
  memory.addFile("pack/sound/ss/bank_meta.yaml", BankMeta);
  memory.addFile("pack/sound/ss/sample_001.wav", monoWav(11025, 32));
  memory.addFile("pack/sound/sl/bank_meta.yaml", BankMeta);
  memory.addFile("pack/sound/mod/bank_meta.yaml", BankMeta);
  memory.addFile("pack/sound/mod/sample_001.wav", monoWav(44100, 32));
  const std::vector<std::filesystem::path> roots{"pack"};
  const auto files = dwe::assets::VirtualFileSystem::mount(memory, roots);
  REQUIRE(files.has_value());

  const auto bank = EffectBank::load(*files, "sound/ss");
  REQUIRE_MESSAGE(bank.has_value(), (bank ? "" : bank.error().message));
  const auto voices = bank->requests(2);
  REQUIRE(voices.has_value());
  REQUIRE(voices->size() == 1);
  CHECK((*voices)[0].clip->sampleRate == 11025);
  CHECK((*voices)[0].clip->frameCount() == 32);
  CHECK((*voices)[0].pitch == doctest::Approx(4.0F));

  // A sample encoded at 44100 Hz that its bank says is at 11025 plays at the chip's rate.
  const auto modded = EffectBank::load(*files, "sound/mod");
  REQUIRE(modded.has_value());
  const auto modVoices = modded->requests(2);
  REQUIRE(modVoices.has_value());
  CHECK((*modVoices)[0].pitch == doctest::Approx(1.0F));

  CHECK_FALSE(EffectBank::load(*files, "sound/sl").has_value());
  CHECK_FALSE(EffectBank::load(*files, "sound/none").has_value());
}
