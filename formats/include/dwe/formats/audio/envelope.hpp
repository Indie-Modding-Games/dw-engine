#pragma once

#include <cstdint>

// The sound chip's ADSR envelope (the two 16-bit registers a tone carries) and its approximation as
// a SoundFont volume envelope.
namespace dwe::formats::audio {

// The fields of the two registers.
struct SpuAdsr {
  bool attackExponential = false;
  std::uint8_t attackShift = 0;  // 0-31
  std::uint8_t attackStep = 0;   // 0-3: the level rises by 7 - step
  std::uint8_t decayShift = 0;   // 0-15; the decay is exponential and falls by 8
  std::uint8_t sustainLevel = 0; // 0-15: (n + 1) * 0x800
  bool sustainExponential = false;
  bool sustainDecrease = false;
  std::uint8_t sustainShift = 0; // 0-31
  std::uint8_t sustainStep = 0;  // 0-3: the level rises by 7 - step or falls by 8 - step
  bool releaseExponential = false;
  std::uint8_t releaseShift = 0; // 0-31; the release falls by 8

  bool operator==(const SpuAdsr&) const = default;
};

// Splits the registers into their fields.
SpuAdsr decodeAdsr(std::uint16_t adsr1, std::uint16_t adsr2);

// The SoundFont volume envelope that comes closest.
struct VolumeEnvelope {
  std::int16_t attack = 0;    // timecents
  std::int16_t decay = 0;     // timecents for a 100 dB fall
  std::int16_t sustain = 0;   // centibels below the peak
  std::int16_t release = 0;   // timecents for a 100 dB fall
  bool slopedSustain = false; // the chip's sustain changes level; a SoundFont sustain cannot

  bool operator==(const VolumeEnvelope&) const = default;
};

// Approximates the chip's envelope by stepping it at 44100 Hz, as the chip does: a phase waits 1 <<
// max(0, shift - 11) samples per step and moves by step << max(0, 11 - shift); an exponential rise
// waits 4 times longer above 0x6000 and an exponential fall scales its step by level / 0x8000.
// Attack is the time from 0 to 0x7FFF. The decay and an exponential release fall at a constant rate
// in decibels, as a SoundFont decay and release do, so their time is scaled to a 100 dB fall; a
// linear release takes its time from 0x7FFF to 0. A falling sustain slope (any rate but 0x7F)
// becomes the decay, towards silence; a rising one is ignored. Both are marked as sloped.
VolumeEnvelope approximateEnvelope(std::uint16_t adsr1, std::uint16_t adsr2);

} // namespace dwe::formats::audio
