#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <string_view>

#include "extractor.hpp"

// The extractor of the original's sound files (SOUND/VHB/*.VHB). Every bank becomes a SoundFont and
// a YAML with its raw VAB fields and what the game plays from it; a sound bank adds one WAV per
// sample, a music font one MIDI file per SEQ. The formats are in docs/formats/audio.md.
namespace dwe::dwx {

// The seven sound files the original loads (formats/audio: soundFiles).
bool handlesSoundFile(std::string_view path);
core::Result<Extraction> extractSoundFile(const Disc& disc, const formats::disc::IsoFile& file);

} // namespace dwe::dwx
