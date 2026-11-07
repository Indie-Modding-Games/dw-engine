#include <dwe/assets/sound.hpp>
#include <dwe/assets/sound_bank.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "media_fixtures.hpp"

using dwe::assets::decodeWav;
using dwe::assets::readSoundBank;
using dwe::assets::testing::ByteWriter;
using dwe::assets::testing::riffFile;

namespace {

std::vector<std::byte> pcmWav(const std::vector<std::int16_t>& samples, std::uint16_t channels,
                              std::uint16_t bits = 16) {
  const auto block = static_cast<std::uint16_t>(channels * bits / 8);
  const auto format =
      ByteWriter{}.u16(1).u16(channels).u32(11025).u32(11025U * block).u16(block).u16(bits).data();
  ByteWriter data;
  for (const std::int16_t sample : samples) {
    data.u16(static_cast<std::uint16_t>(sample));
  }
  return riffFile("WAVE", ByteWriter{}.chunk("fmt ", format).chunk("data", data.data()).data());
}

// A smpl chunk with a unity note and, when loopEnd is given, one loop (its end inclusive).
std::vector<std::byte> withSampler(std::vector<std::byte> wav, std::uint32_t note,
                                   std::uint32_t loopStart, std::uint32_t loopEnd) {
  ByteWriter sampler;
  sampler.zeros(12).u32(note).zeros(12).u32(1).u32(0);
  sampler.u32(0).u32(0).u32(loopStart).u32(loopEnd).u32(0).u32(0);
  const auto chunk = ByteWriter{}.chunk("smpl", sampler.data()).data();
  wav.insert(wav.end(), chunk.begin(), chunk.end());
  const auto riffSize = static_cast<std::uint32_t>(wav.size() - 8);
  for (std::size_t i = 0; i < 4; ++i) {
    wav[4 + i] = static_cast<std::byte>((riffSize >> (8 * i)) & 0xFFU);
  }
  return wav;
}

const std::string bankText = R"(vab:
  master_volume: 100
programs:
- program: 0
  master_volume: 127
  pan: 64
  tones:
  - tone: 0
    keys: [60, 60]
    center: 60
    shift: 0
    volume: 100
    pan: 64
    sample: 1
    adsr: "80ff 5fd5"
  - tone: 1
    center: 72
    shift: 64
    volume: 50
    pan: 0
    sample: 2
    adsr: "a8ff 5fde"
    mode: 4
samples:
- sample: 1
  file: "sample_001.wav"
  rate: 11025
- sample: 2
  file: "sample_002.wav"
  rate: 22050
sounds:
- value: 0
  program: 0
  note: 60
  tones: [0]
- value: 3
  program: 0
  note: 72
  tones: [0, 1]
)";

// A WAV of two silent mono 16-bit frames whose 'fmt ' chunk has this format tag; the extensible
// tag gets the 40-byte chunk with the sub-format GUID whose first two bytes are `subFormat`.
std::vector<std::byte> taggedWav(std::uint16_t tag, std::uint16_t subFormat = 1) {
  ByteWriter format;
  format.u16(tag).u16(1).u32(11025).u32(22050).u16(2).u16(16);
  if (tag == 0xFFFE) {
    format.u16(22).u16(16).u32(4).u16(subFormat).u16(0).u16(0).u16(0x10);
    format.u16(0x0080).u32(0x3800AA00).u16(0x719B);
  }
  return riffFile(
      "WAVE",
      ByteWriter{}.chunk("fmt ", format.data()).chunk("data", ByteWriter{}.zeros(4).data()).data());
}

std::string replaced(std::string text, const std::string& from, const std::string& to) {
  text.replace(text.find(from), from.size(), to);
  return text;
}

} // namespace

TEST_CASE("a 16-bit WAV decodes with the loop and the unity note of its smpl chunk") {
  const auto mono = decodeWav(withSampler(pcmWav({1, -2, 3, 4, 5}, 1), 72, 1, 3));
  REQUIRE_MESSAGE(mono.has_value(), (mono ? "" : mono.error().message));
  CHECK(mono->sampleRate == 11025);
  CHECK(mono->sourceRate == 11025);
  CHECK(mono->channels == 1);
  CHECK(mono->pcm == std::vector<std::int16_t>{1, -2, 3, 4, 5});
  CHECK(mono->unityNote == 72);
  CHECK(mono->loop == dwe::assets::SoundLoop{.start = 1, .length = 3});

  const auto stereo = decodeWav(pcmWav({1, 2, 3, 4}, 2));
  REQUIRE(stereo.has_value());
  CHECK(stereo->channels == 2);
  CHECK(stereo->pcm.size() == 4);
  CHECK_FALSE(stereo->loop.has_value());
}

TEST_CASE("WAV files the player does not take are refused") {
  CHECK_FALSE(decodeWav(pcmWav({1, 2}, 1, 8)).has_value());
  CHECK_FALSE(decodeWav(pcmWav({1, 2, 3}, 3)).has_value());
  CHECK_FALSE(decodeWav(withSampler(pcmWav({1, 2, 3}, 1), 60, 1, 3)).has_value());
  CHECK_FALSE(decodeWav(withSampler(pcmWav({1, 2, 3}, 1), 60, 2, 1)).has_value());
  CHECK_FALSE(decodeWav(withSampler(pcmWav({1, 2, 3}, 1), 128, 0, 1)).has_value());
}

TEST_CASE("a bank_meta reads its volumes, tones, samples and sounds") {
  const auto bank = readSoundBank(bankText);
  REQUIRE_MESSAGE(bank.has_value(), (bank ? "" : bank.error().message));
  CHECK(bank->volume == 100);
  const auto* program = bank->program(0);
  REQUIRE(program != nullptr);
  REQUIRE(program->tones.size() == 2);
  CHECK(program->tones[1].root == 72);
  CHECK(program->tones[1].fineTune == 64);
  CHECK(program->tones[1].pan == 0);
  CHECK(program->tones[1].adsr1 == 0xa8ff);
  CHECK(program->tones[1].adsr2 == 0x5fde);
  // The reverb bit of the tone's mode; a tone without a mode has none.
  CHECK(program->tones[1].reverb());
  CHECK_FALSE(program->tones[0].reverb());
  REQUIRE(bank->sample(2) != nullptr);
  CHECK(bank->sample(2)->rate == 22050);
  CHECK(bank->sample(2)->file == "sample_002.wav");
  REQUIRE(bank->sound(3) != nullptr);
  CHECK(bank->sound(3)->tones == std::vector<std::uint32_t>{0, 1});
  CHECK(bank->sound(1) == nullptr);
}

TEST_CASE("a bank_meta whose sounds would not play is refused") {
  CHECK_FALSE(readSoundBank(replaced(bankText, "tones: [0, 1]", "tones: [0, 2]")).has_value());
  CHECK_FALSE(
      readSoundBank(replaced(bankText, "  program: 0\n  note: 60", "  program: 1\n  note: 60"))
          .has_value());
  CHECK_FALSE(
      readSoundBank(replaced(bankText, "sample: 2\n    adsr", "sample: 9\n    adsr")).has_value());
  CHECK_FALSE(readSoundBank(replaced(bankText, "\"80ff 5fd5\"", "\"80ff5fd5\"")).has_value());
  CHECK_FALSE(readSoundBank(replaced(bankText, "\"80ff 5fd5\"", "\"80fg 5fd5\"")).has_value());
  CHECK_FALSE(readSoundBank(replaced(bankText, "rate: 22050", "rate: 0")).has_value());
  CHECK_FALSE(readSoundBank(replaced(bankText, "volume: 50", "volume: 200")).has_value());
}

TEST_CASE("a WAV sound is integer PCM: plain or extensible with the PCM sub-format") {
  CHECK(decodeWav(taggedWav(1)).has_value());
  CHECK(decodeWav(taggedWav(0xFFFE)).has_value());
  CHECK_FALSE(decodeWav(taggedWav(3)).has_value());
  CHECK_FALSE(decodeWav(taggedWav(0xFFFE, 3)).has_value());
}

TEST_CASE("broken WAV headers and chunks are refused") {
  CHECK_FALSE(decodeWav(pcmWav({}, 0)).has_value());
  auto oversized = pcmWav({1, 2}, 1);
  oversized[4] = std::byte{0xFF};
  oversized[5] = std::byte{0xFF};
  CHECK_FALSE(decodeWav(oversized).has_value());
  // A smpl chunk that says it has a loop but ends before it.
  auto truncated = withSampler(pcmWav({1, 2, 3}, 1), 60, 0, 1);
  truncated.resize(truncated.size() - 24);
  const auto riffSize = static_cast<std::uint32_t>(truncated.size() - 8);
  const auto smplAt = truncated.size() - 36 - 4;
  for (std::size_t i = 0; i < 4; ++i) {
    truncated[4 + i] = static_cast<std::byte>((riffSize >> (8 * i)) & 0xFFU);
    truncated[smplAt + i] = static_cast<std::byte>((36U >> (8 * i)) & 0xFFU);
  }
  CHECK_FALSE(decodeWav(truncated).has_value());
}

TEST_CASE("a bank_meta that gives a program, tone, sample or sound twice is refused") {
  CHECK_FALSE(readSoundBank(replaced(bankText, "  - tone: 1\n", "  - tone: 0\n")).has_value());
  CHECK_FALSE(readSoundBank(replaced(bankText, "- sample: 2\n", "- sample: 1\n")).has_value());
  CHECK_FALSE(readSoundBank(replaced(bankText, "- value: 3\n", "- value: 0\n")).has_value());
  CHECK_FALSE(readSoundBank(replaced(bankText, "samples:\n",
                                     "- program: 0\n  master_volume: 127\n  pan: 64\n  tones: []\n"
                                     "samples:\n"))
                  .has_value());
}
