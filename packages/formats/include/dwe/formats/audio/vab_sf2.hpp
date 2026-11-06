#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/audio/adpcm.hpp>
#include <dwe/formats/audio/sf2.hpp>
#include <dwe/formats/audio/vab.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// The VAB as a SoundFont. What maps and how is in docs/formats/audio.md; it is an approximation of
// the sound library and the sound chip, not a copy of them.
namespace dwe::formats::audio {

struct VabSoundFont {
  SoundFont font;
  std::size_t slopedSustains = 0; // tones whose sustain slope only the decay approximates
};

// The attenuation of a tone in centibels: the product of the tone, program and bank volumes (0-127
// each) as an amplitude ratio.
std::int16_t toneAttenuation(std::uint8_t toneVolume, std::uint8_t programVolume,
                             std::uint8_t bankVolume);

// The pan of a tone in 0.1 % (-500 left, 500 right): the tone's and the program's offsets from 64,
// added and clamped.
std::int16_t tonePan(std::uint8_t tonePan, std::uint8_t programPan);

// The fine tune of a tone in cents. The sound library plays a key at (note * 128) - (center * 128 +
// shift) 128ths of a semitone from the sample's own pitch, so shift raises the centre and lowers
// the sound: -shift * 100 / 128.
std::int16_t toneFineTune(std::uint8_t shift);

// Builds a SoundFont from a VAB and its decoded samples, indexed by sample number (entry 0 unused).
// Each program with tones becomes a preset (bank 0, preset = program number) playing an instrument
// with one zone per tone: key range min-max, root key = center, fine tune, attenuation, pan, reverb
// send (mode bit 2), the volume envelope of the ADSR registers and the sample's loop. Every sample
// a tone plays becomes a SoundFont sample at 44100 Hz.
core::Result<VabSoundFont> convertVabToSoundFont(const Vab& vab,
                                                 std::span<const DecodedSample> samples,
                                                 std::string_view name);

} // namespace dwe::formats::audio
