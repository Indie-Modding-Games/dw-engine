#pragma once

#include <dwe/formats/audio/seq.hpp>
#include <dwe/formats/audio/vab.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::formats::audio {

// The pitch bend range of each program, in semitones, or nothing for an empty slot.
using BendRanges = std::array<std::optional<std::uint8_t>, VabProgramSlots>;

// Returns the pitch bend range of each program of a VAB: its first tone's pbmax. The sound library bends each voice
// by its own tone's range; a MIDI channel has one range, so tones of one program with other ranges bend wrongly.
BendRanges pitchBendRanges(const Vab& vab);

// The marker texts of the loop.
inline constexpr std::string_view MidiLoopStartMarker = "loopStart";
inline constexpr std::string_view MidiLoopEndMarker = "loopEnd";

// Writes a SEQ as a Standard MIDI File of format 0 with the SEQ's resolution as the division, so ticks and tempos stay
// exact. The header's tempo becomes a tempo event at tick 0. Every program change is followed by RPN 0 (pitch bend
// range) when its program has a range, and RPN null. The loop markers become marker meta events "loopStart" and
// "loopEnd"; their count is not written. Notes keep their velocity-0 note-ons.
std::vector<std::byte> convertSeqToMidi(const Seq& seq, const BendRanges& bendRanges);

} // namespace dwe::formats::audio
