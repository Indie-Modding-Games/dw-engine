#include <dwe/formats/audio/envelope.hpp>
#include <dwe/formats/audio/midi.hpp>
#include <dwe/formats/audio/sf2.hpp>
#include <dwe/formats/audio/vab_sf2.hpp>
#include <dwe/formats/audio/wav.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "audio_fixtures.hpp"
#include "riff_reader.hpp"

using namespace dwe::formats::audio;
using namespace dwe::formats::audio::test;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

std::int16_t expectedTimecents(double samples) {
  return static_cast<std::int16_t>(std::round(1200.0 * std::log2(samples / 44100.0)));
}

// A font with two samples (the second looped), one instrument of two zones and two presets that
// play it.
SoundFont smallFont() {
  SoundFont font;
  font.name = "test font";
  font.samples.push_back(Sf2Sample{.name = "one",
                                   .pcm = {1, 2, 3},
                                   .sampleRate = 22050,
                                   .loop = {},
                                   .originalPitch = 60,
                                   .pitchCorrection = 0});
  font.samples.push_back(Sf2Sample{.name = "two",
                                   .pcm = {10, 20, 30, 40},
                                   .sampleRate = 44100,
                                   .loop = SampleLoop{.start = 1, .end = 4},
                                   .originalPitch = 48,
                                   .pitchCorrection = -3});
  font.instruments.push_back(Sf2Instrument{
      .name = "instrument",
      .zones = {Sf2Zone{.keyLow = 0, .keyHigh = 59, .generators = {}, .sample = 0},
                Sf2Zone{.keyLow = 60,
                        .keyHigh = 127,
                        .generators = {{.generator = Sf2Generator::FineTune, .amount = -25},
                                       {.generator = Sf2Generator::SampleModes, .amount = 1}},
                        .sample = 1}}});
  font.presets.push_back(Sf2Preset{.name = "first", .preset = 0, .bank = 0, .instrument = 0});
  font.presets.push_back(Sf2Preset{.name = "second", .preset = 5, .bank = 0, .instrument = 0});
  return font;
}

// The (status, data...) bytes and absolute tick of each event of the single track of a format 0
// file.
struct MidiEvent {
  std::uint32_t tick = 0;
  std::vector<std::uint8_t> bytes;
};

std::vector<MidiEvent> readMidiTrack(const std::vector<std::byte>& file, std::uint16_t& division) {
  const auto u8 = [&file](std::size_t at) { return std::to_integer<std::uint8_t>(file[at]); };
  const auto be = [&u8](std::size_t at, std::size_t size) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
      value = (value << 8U) | u8(at + i);
    }
    return value;
  };
  REQUIRE(file.size() >= 22);
  CHECK(readTag(file, 0) == "MThd");
  CHECK(be(4, 4) == 6);
  CHECK(be(8, 2) == 0);
  CHECK(be(10, 2) == 1);
  division = static_cast<std::uint16_t>(be(12, 2));
  CHECK(readTag(file, 14) == "MTrk");
  REQUIRE(be(18, 4) == file.size() - 22);

  std::vector<MidiEvent> events;
  std::size_t at = 22;
  std::uint32_t tick = 0;
  while (at < file.size()) {
    std::uint32_t delta = 0;
    std::uint8_t byte = 0;
    do {
      byte = u8(at++);
      delta = (delta << 7U) | (byte & 0x7FU);
    } while ((byte & 0x80U) != 0);
    tick += delta;
    MidiEvent event{.tick = tick, .bytes = {u8(at++)}};
    const std::uint8_t status = event.bytes[0];
    REQUIRE((status & 0x80U) != 0); // the writer never uses running status
    std::size_t length = 0;
    if (status == 0xFF) {
      event.bytes.push_back(u8(at++));
      length = u8(at);
      event.bytes.push_back(u8(at++));
    } else {
      length = (status & 0xF0U) == 0xC0 || (status & 0xF0U) == 0xD0 ? 1 : 2;
    }
    for (std::size_t i = 0; i < length; ++i) {
      event.bytes.push_back(u8(at++));
    }
    events.push_back(event);
  }
  CHECK(at == file.size());
  return events;
}

} // namespace

TEST_CASE("WAV: a PCM file, with a smpl chunk only for a loop") {
  const std::vector<std::int16_t> pcm = {0, 1000, -1000, 32767, -32768};
  const auto plain =
      writeWav(WavSound{.pcm = pcm, .sampleRate = 11025, .loop = {}, .unityNote = 60});
  const auto riff = parseRiff(plain);
  REQUIRE(riff.id == "RIFF");
  CHECK(riff.type == "WAVE");
  const RiffChunk* format = riff.child("fmt ");
  REQUIRE(format != nullptr);
  REQUIRE(format->data.size() == 16);
  CHECK(readU16(format->data, 0) == 1);
  CHECK(readU16(format->data, 2) == 1);
  CHECK(readU32(format->data, 4) == 11025);
  CHECK(readU32(format->data, 8) == 22050);
  CHECK(readU16(format->data, 14) == 16);
  const RiffChunk* data = riff.child("data");
  REQUIRE(data != nullptr);
  REQUIRE(data->data.size() == 10);
  CHECK(static_cast<std::int16_t>(readU16(data->data, 6)) == 32767);
  CHECK(static_cast<std::int16_t>(readU16(data->data, 8)) == -32768);
  CHECK(riff.child("smpl") == nullptr);

  const auto looped = writeWav(WavSound{
      .pcm = pcm, .sampleRate = 44100, .loop = SampleLoop{.start = 1, .end = 5}, .unityNote = 48});
  const auto loopedRiff = parseRiff(looped);
  REQUIRE(loopedRiff.id == "RIFF");
  const RiffChunk* sampler = loopedRiff.child("smpl");
  REQUIRE(sampler != nullptr);
  REQUIRE(sampler->data.size() == 36 + 24);
  CHECK(readU32(sampler->data, 12) == 48);
  CHECK(readU32(sampler->data, 28) == 1);
  CHECK(readU32(sampler->data, 36 + 8) == 1);
  CHECK(readU32(sampler->data, 36 + 12) == 4);
  CHECK(writeWav(WavSound{.pcm = pcm,
                          .sampleRate = 44100,
                          .loop = SampleLoop{.start = 1, .end = 5},
                          .unityNote = 48}) == looped);
}

TEST_CASE("WAV: what writeWav writes reads back") {
  const std::vector<std::int16_t> pcm = {0, 1000, -1000, 32767, -32768};
  const auto plain =
      readWav(writeWav(WavSound{.pcm = pcm, .sampleRate = 11025, .loop = {}, .unityNote = 60}));
  REQUIRE(plain.has_value());
  CHECK(plain->sampleRate == 11025);
  CHECK(plain->channels == 1);
  CHECK(plain->pcm == pcm);
  CHECK_FALSE(plain->loop.has_value());
  CHECK(plain->unityNote == 60);

  const auto looped = readWav(writeWav(WavSound{
      .pcm = pcm, .sampleRate = 5513, .loop = SampleLoop{.start = 1, .end = 5}, .unityNote = 48}));
  REQUIRE(looped.has_value());
  CHECK(looped->sampleRate == 5513);
  CHECK(looped->loop == SampleLoop{.start = 1, .end = 5});
  CHECK(looped->unityNote == 48);
}

TEST_CASE("WAV: files readWav does not take are errors") {
  const std::vector<std::int16_t> pcm = {1, 2, 3, 4};
  const auto good =
      writeWav(WavSound{.pcm = pcm, .sampleRate = 22050, .loop = {}, .unityNote = 60});
  const auto errorOf = [&](auto change) {
    auto bytes = good;
    change(bytes);
    const auto wav = readWav(bytes);
    REQUIRE_FALSE(wav.has_value());
    return wav.error().message;
  };
  // The fmt chunk's fields start at 20: format, channels, rate, bytes per second, block size, bits.
  const auto put16 = [](std::vector<std::byte>& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<std::byte>(value & 0xFFU);
    bytes[at + 1] = static_cast<std::byte>(value >> 8U);
  };
  CHECK(errorOf([](auto& bytes) { bytes[0] = std::byte{'X'}; }) == "not a RIFF WAVE file");
  CHECK(errorOf([](auto& bytes) { bytes.pop_back(); }) == "its RIFF size does not match the file");
  CHECK(errorOf([&](auto& bytes) { put16(bytes, 20, 3); }) ==
        "it is not 16-bit PCM (format 3, 16 bits)");
  CHECK(errorOf([&](auto& bytes) { put16(bytes, 34, 8); }) ==
        "it is not 16-bit PCM (format 1, 8 bits)");
  CHECK(errorOf([&](auto& bytes) { put16(bytes, 22, 3); }) ==
        "it has 3 channels; only 1 or 2 are read");
  CHECK(errorOf([&](auto& bytes) { put16(bytes, 22, 2); }) ==
        "its block size does not match its channels");
  CHECK(errorOf([&](auto& bytes) {
          put16(bytes, 22, 2);
          put16(bytes, 32, 4);
          put16(bytes, 40, 6); // data: 6 bytes, a frame and a half
          put16(bytes, 4, 42); // and the RIFF ends with it
        }) == "its data is not a whole number of frames");
  CHECK(errorOf([&](auto& bytes) {
          put16(bytes, 40, 9);
        }).starts_with("the chunk at 36 does not fit"));
  CHECK(errorOf([&](auto& bytes) { bytes[36] = std::byte{'x'}; }) == "it has no data chunk");

  const auto looped = writeWav(WavSound{
      .pcm = pcm, .sampleRate = 22050, .loop = SampleLoop{.start = 0, .end = 4}, .unityNote = 60});
  auto pastEnd = looped;
  // The loop's inclusive end, in the smpl chunk that follows the fmt chunk.
  pastEnd[36 + 8 + 36 + 12] = std::byte{9};
  const auto wav = readWav(pastEnd);
  REQUIRE_FALSE(wav.has_value());
  CHECK(wav.error().message == "its loop goes past its last frame");
}

TEST_CASE("SF2: the chunks, tables and terminal records") {
  const auto bytes = writeSf2(smallFont());
  REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
  const auto again = writeSf2(smallFont());
  REQUIRE(again.has_value());
  CHECK(*again == *bytes);
  const auto riff = parseRiff(*bytes);
  REQUIRE(riff.id == "RIFF");
  CHECK(riff.type == "sfbk");
  REQUIRE(riff.children.size() == 3);
  const RiffChunk* info = riff.child("INFO");
  REQUIRE(info != nullptr);
  REQUIRE(info->child("ifil") != nullptr);
  CHECK(readU16(info->child("ifil")->data, 0) == 2);
  REQUIRE(info->child("INAM") != nullptr);
  CHECK(info->child("INAM")->data.size() % 2 == 0);

  const RiffChunk* sampleData = riff.child("sdta");
  REQUIRE(sampleData != nullptr);
  REQUIRE(sampleData->child("smpl") != nullptr);
  // 3 + 46 points, then 4 + 8 guard + 46.
  CHECK(sampleData->child("smpl")->data.size() == 2 * (3 + 46 + 4 + 8 + 46));

  const RiffChunk* presetData = riff.child("pdta");
  REQUIRE(presetData != nullptr);
  const std::vector<std::pair<std::string, std::size_t>> tables = {
      {"phdr", 38 * 3}, {"pbag", 4 * 3},           {"pmod", 10},
      {"pgen", 4 * 3},  {"inst", 22 * 2},          {"ibag", 4 * 3},
      {"imod", 10},     {"igen", 4 * (2 + 4 + 1)}, {"shdr", 46 * 3}};
  REQUIRE(presetData->children.size() == tables.size());
  for (std::size_t i = 0; i < tables.size(); ++i) {
    CHECK(presetData->children[i].id == tables[i].first);
    CHECK(presetData->children[i].data.size() == tables[i].second);
  }
  const auto igen = presetData->child("igen")->data;
  CHECK(readU16(igen, 0) == 43); // key range first
  CHECK(readU16(igen, 2) == (0 | (59 << 8)));
  CHECK(readU16(igen, 4) == 53); // sample last
  CHECK(readU16(igen, 8) == 43);
  CHECK(readU16(igen, 12) == 52);
  CHECK(static_cast<std::int16_t>(readU16(igen, 14)) == -25);
  CHECK(readU16(igen, 20) == 53);
  CHECK(readU16(igen, 22) == 1);
  const auto pgen = presetData->child("pgen")->data;
  CHECK(readU16(pgen, 0) == 41);
  const auto phdr = presetData->child("phdr")->data;
  CHECK(readU16(phdr, 38 + 20) == 5);
  CHECK(readU16(phdr, (2 * 38) + 24) == 2); // the terminal record's bag index
  const auto shdr = presetData->child("shdr")->data;
  CHECK(readU32(shdr, 46 + 20) == 49);      // start of "two"
  CHECK(readU32(shdr, 46 + 24) == 49 + 12); // end, guard points included
  CHECK(readU32(shdr, 46 + 28) == 49 + 1);  // loop start
  CHECK(readU32(shdr, 46 + 32) == 49 + 4);  // loop end
  CHECK(readU32(shdr, 46 + 36) == 44100);
  CHECK(std::to_integer<std::uint8_t>(shdr[46 + 40]) == 48);
  CHECK(std::to_integer<std::int8_t>(shdr[46 + 41]) == -3);
  CHECK(readU16(shdr, 46 + 44) == 1);
  CHECK(readTag(shdr, std::size_t{2} * 46) == std::string("EOS\0", 4));
}

TEST_CASE("SF2: inconsistent fonts are errors") {
  SoundFont font = smallFont();
  font.instruments[0].zones[0].sample = 7;
  CHECK_FALSE(writeSf2(font).has_value());
  font = smallFont();
  font.samples[1].loop = SampleLoop{.start = 2, .end = 9};
  CHECK_FALSE(writeSf2(font).has_value());
  font = smallFont();
  font.presets[0].instrument = 3;
  CHECK_FALSE(writeSf2(font).has_value());
  font = smallFont();
  font.instruments[0].zones[0].generators.push_back(
      {.generator = Sf2Generator::KeyRange, .amount = 0});
  CHECK_FALSE(writeSf2(font).has_value());
  font = smallFont();
  font.instruments[0].zones[0].keyLow = 70;
  CHECK_FALSE(writeSf2(font).has_value());
}

TEST_CASE("SPU envelope: register fields and SoundFont times") {
  CHECK(decodeAdsr(0x80FF, 0x5FC0) == SpuAdsr{.attackExponential = true,
                                              .attackShift = 0,
                                              .attackStep = 0,
                                              .decayShift = 15,
                                              .sustainLevel = 15,
                                              .sustainExponential = false,
                                              .sustainDecrease = true,
                                              .sustainShift = 31,
                                              .sustainStep = 3,
                                              .releaseExponential = false,
                                              .releaseShift = 0});

  SUBCASE("a linear attack of shift 20 waits 512 samples per step of 7") {
    const auto envelope = approximateEnvelope(0x500F, 0x1FC0 | 20);
    CHECK(envelope.attack == expectedTimecents(4681.0 * 512));
    CHECK(envelope.decay == -12000); // sustain level 15 is the peak: no decay
    CHECK(envelope.sustain == 0);
    CHECK(envelope.release == expectedTimecents(4096.0 * 512)); // linear: 0x7FFF / 8 steps
    CHECK_FALSE(envelope.slopedSustain);
  }
  SUBCASE("fast phases clamp to the shortest time") {
    const auto envelope = approximateEnvelope(0x000F, 0x1FC0);
    CHECK(envelope.attack == -12000);
    CHECK(envelope.release == -12000);
  }
  SUBCASE("the sustain level is in centibels below the peak") {
    CHECK(approximateEnvelope(0x0000, 0x1FC0).sustain == 241);
    CHECK(approximateEnvelope(0x0007, 0x1FC0).sustain == 60);
  }
  SUBCASE("an exponential attack is slower than a linear one; a longer decay shift decays slower") {
    CHECK(approximateEnvelope(0xD000, 0x1FC0).attack > approximateEnvelope(0x5000, 0x1FC0).attack);
    CHECK(approximateEnvelope(0x00F0, 0x1FC0).decay > approximateEnvelope(0x0050, 0x1FC0).decay);
    CHECK(approximateEnvelope(0x0000, 0x1FC0 | 0x20 | 20).release >
          approximateEnvelope(0x0000, 0x1FC0 | 0x20 | 10).release);
  }
  SUBCASE("a falling sustain slope becomes the decay towards silence") {
    const auto envelope = approximateEnvelope(0x000F, 0xC000 | (20 << 8));
    CHECK(envelope.slopedSustain);
    CHECK(envelope.sustain == 1000);
    CHECK(envelope.decay > 0);
    const auto rising = approximateEnvelope(0x0007, 20 << 8);
    CHECK(rising.slopedSustain);
    CHECK(rising.sustain == 60);
  }
}

TEST_CASE("VAB to SoundFont: volume, pan and tuning") {
  CHECK(toneAttenuation(127, 127, 127) == 0);
  CHECK(toneAttenuation(64, 127, 127) == 60);
  CHECK(toneAttenuation(0, 127, 127) == 1440);
  CHECK(tonePan(64, 64) == 0);
  CHECK(tonePan(0, 64) == -500);
  CHECK(tonePan(127, 64) == 492);
  CHECK(tonePan(127, 127) == 500);
  CHECK(toneFineTune(0) == 0);
  CHECK(toneFineTune(64) == -50);
  CHECK(toneFineTune(32) == -25);
}

TEST_CASE("VAB to SoundFont: a preset per program, a zone per tone, a sample per played VAG") {
  const std::vector<ProgramSpec> programs = {
      ProgramSpec{.slot = 0,
                  .tones = {ToneSpec{.min = 0, .max = 59, .vag = 1},
                            ToneSpec{.min = 60, .max = 72, .vag = 1}},
                  .mvol = 127,
                  .mpan = 64},
      ProgramSpec{.slot = 4,
                  .tones = {ToneSpec{.center = 48, .shift = 64, .mode = 4, .vag = 2}},
                  .mvol = 127,
                  .mpan = 64}};
  const auto vab = readVab(buildVab(programs, {32, 48}, 2));
  REQUIRE_MESSAGE(vab.has_value(), errorOf(vab));
  std::vector<DecodedSample> samples(3);
  samples[1] = decodeAdpcm(adpcmSample(std::array<std::uint8_t, 2>{0, AdpcmFlags::End}));
  samples[2] = decodeAdpcm(adpcmSample(
      std::array<std::uint8_t, 3>{0, AdpcmFlags::LoopStart, AdpcmFlags::End | AdpcmFlags::Repeat}));
  const auto converted = convertVabToSoundFont(*vab, samples, "bank");
  REQUIRE_MESSAGE(converted.has_value(), errorOf(converted));
  const SoundFont& font = converted->font;
  REQUIRE(font.presets.size() == 2);
  CHECK(font.presets[1].preset == 4);
  CHECK(font.presets[1].instrument == 1);
  REQUIRE(font.samples.size() == 2); // VAG 1 is shared by two tones
  CHECK(font.samples[1].loop == SampleLoop{.start = 28, .end = 84});
  CHECK(font.samples[1].originalPitch == 48);
  REQUIRE(font.instruments[0].zones.size() == 2);
  CHECK(font.instruments[0].zones[1].keyLow == 60);
  CHECK(font.instruments[0].zones[1].keyHigh == 72);
  const auto& zone = font.instruments[1].zones[0];
  const auto generator = [&zone](Sf2Generator which) {
    for (const auto& value : zone.generators) {
      if (value.generator == which) {
        return std::optional{value.amount};
      }
    }
    return std::optional<std::int16_t>{};
  };
  CHECK(generator(Sf2Generator::OverridingRootKey) == 48);
  CHECK(generator(Sf2Generator::FineTune) == -50);
  CHECK(generator(Sf2Generator::ReverbEffectsSend) == 1000);
  CHECK(generator(Sf2Generator::SampleModes) == 1);
  CHECK(converted->slopedSustains == 0);
  CHECK(writeSf2(font).has_value());

  samples[2] = DecodedSample{};
  CHECK_FALSE(convertVabToSoundFont(*vab, samples, "bank").has_value());
}

TEST_CASE("SEQ to MIDI: exact ticks, tempo, bend range after program changes and loop markers") {
  Seq seq;
  seq.header = SeqHeader{.version = 1,
                         .resolution = 480,
                         .tempo = 500000,
                         .rhythmNumerator = 1,
                         .rhythmDenominator = 4};
  const auto event = [](std::uint32_t tick, SeqEventType type, std::uint8_t channel,
                        std::uint8_t data1, std::uint8_t data2, std::uint32_t tempo = 0) {
    return SeqEvent{.tick = tick,
                    .type = type,
                    .channel = channel,
                    .data1 = data1,
                    .data2 = data2,
                    .tempo = tempo};
  };
  seq.events = {event(0, SeqEventType::Program, 2, 4, 0),
                event(0, SeqEventType::LoopStart, 0, 127, 0),
                event(0, SeqEventType::NoteOn, 2, 60, 100),
                event(200, SeqEventType::NoteOn, 2, 60, 0),
                event(300, SeqEventType::Tempo, 0, 0, 0, 0x123456),
                event(20000, SeqEventType::PitchBend, 2, 1, 2),
                event(20000, SeqEventType::LoopEnd, 0, 0, 0),
                event(20000, SeqEventType::End, 0, 0, 0)};
  BendRanges ranges{};
  ranges[4] = 12;
  const auto file = convertSeqToMidi(seq, ranges);
  CHECK(convertSeqToMidi(seq, ranges) == file);
  std::uint16_t division = 0;
  const auto events = readMidiTrack(file, division);
  CHECK(division == 480);
  REQUIRE(events.size() == 15);
  CHECK(events[0].bytes == std::vector<std::uint8_t>{0xFF, 0x51, 3, 0x07, 0xA1, 0x20});
  CHECK(events[1].bytes == std::vector<std::uint8_t>{0xC2, 4});
  CHECK(events[2].bytes == std::vector<std::uint8_t>{0xB2, 101, 0});
  CHECK(events[4].bytes == std::vector<std::uint8_t>{0xB2, 6, 12});
  CHECK(events[8].bytes ==
        std::vector<std::uint8_t>{0xFF, 0x06, 9, 'l', 'o', 'o', 'p', 'S', 't', 'a', 'r', 't'});
  CHECK(events[10].tick == 200);
  CHECK(events[10].bytes == std::vector<std::uint8_t>{0x92, 60, 0});
  CHECK(events[11].tick == 300);
  CHECK(events[11].bytes == std::vector<std::uint8_t>{0xFF, 0x51, 3, 0x12, 0x34, 0x56});
  CHECK(events[12].tick == 20000);
  CHECK(events[12].bytes == std::vector<std::uint8_t>{0xE2, 1, 2});
  CHECK(events[13].bytes ==
        std::vector<std::uint8_t>{0xFF, 0x06, 7, 'l', 'o', 'o', 'p', 'E', 'n', 'd'});
  CHECK(events[14].bytes == std::vector<std::uint8_t>{0xFF, 0x2F, 0});

  SUBCASE("a program without a range gets no RPN") {
    const auto plain = readMidiTrack(convertSeqToMidi(seq, BendRanges{}), division);
    CHECK(plain.size() == 9);
  }
}

TEST_CASE("SEQ to MIDI: the bend range of a program is its first tone's") {
  const std::vector<ProgramSpec> programs = {ProgramSpec{
      .slot = 2, .tones = {ToneSpec{.pbmax = 12}, ToneSpec{.pbmax = 2}}, .mvol = 127, .mpan = 64}};
  const auto vab = readVab(buildVab(programs, {32}, 1));
  REQUIRE(vab.has_value());
  const auto ranges = pitchBendRanges(*vab);
  CHECK(ranges[2] == 12);
  CHECK_FALSE(ranges[0].has_value());
}
