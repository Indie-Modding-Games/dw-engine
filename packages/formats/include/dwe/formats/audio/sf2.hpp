#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/audio/adpcm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// A SoundFont 2 writer: samples, instruments of zones over key ranges, and presets that each play
// one instrument.
namespace dwe::formats::audio {

// The generators the converters use, with their SoundFont 2.04 numbers.
enum class Sf2Generator : std::uint8_t {
  ReverbEffectsSend = 16, // 0.1 % units
  Pan = 17,               // 0.1 % units, -500 = left
  AttackVolEnv = 34,      // timecents
  HoldVolEnv = 35,
  DecayVolEnv = 36,
  SustainVolEnv = 37, // centibels below the peak
  ReleaseVolEnv = 38,
  Instrument = 41,
  KeyRange = 43,
  InitialAttenuation = 48, // centibels
  FineTune = 52,           // cents
  SampleId = 53,
  SampleModes = 54, // 1 = loop
  OverridingRootKey = 58,
};

struct Sf2GeneratorValue {
  Sf2Generator generator = Sf2Generator::Pan;
  std::int16_t amount = 0;

  bool operator==(const Sf2GeneratorValue&) const = default;
};

// A mono sample. The writer adds the points the format asks for after it: a copy of the loop's
// first 8 points for a looped sample (so interpolation at the loop end sees the loop start) and 46
// zeros.
struct Sf2Sample {
  std::string name; // up to 20 characters
  std::vector<std::int16_t> pcm;
  std::uint32_t sampleRate = 44100;
  std::optional<SampleLoop> loop;
  std::uint8_t originalPitch = 60;
  std::int8_t pitchCorrection = 0;
};

// A zone of an instrument: a key range, its generators and the sample it plays. The writer puts the
// key range first and the sample last, as the format wants.
struct Sf2Zone {
  std::uint8_t keyLow = 0;
  std::uint8_t keyHigh = 127;
  std::vector<Sf2GeneratorValue> generators;
  std::size_t sample = 0;
};

struct Sf2Instrument {
  std::string name;
  std::vector<Sf2Zone> zones;
};

// A preset: one zone over every key that plays an instrument.
struct Sf2Preset {
  std::string name;
  std::uint16_t preset = 0;
  std::uint16_t bank = 0;
  std::size_t instrument = 0;
};

struct SoundFont {
  std::string name;
  std::vector<Sf2Sample> samples;
  std::vector<Sf2Instrument> instruments;
  std::vector<Sf2Preset> presets;
};

// Writes a RIFF sfbk file (version 2.01): INFO, the 16-bit samples, and the nine preset data
// tables, each with its terminal record. Fails when an index or a loop is out of range, a generator
// is given that the writer places itself (KeyRange, SampleId, Instrument), or a table outgrows its
// 16-bit indices.
core::Result<std::vector<std::byte>> writeSf2(const SoundFont& font);

} // namespace dwe::formats::audio
