#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The music as dwx extracts it (docs/formats/audio.md): music/<font>/font.sf2, one track_<nn>.mid
// per song of the font and font_meta.yaml, which says how each song plays.
namespace dwe::assets {

// A channel event or a tempo change of a song, at its tick.
struct SongEvent {
  enum class Kind : std::uint8_t {
    NoteOn,
    NoteOff,
    Control,
    Program,
    PitchBend,
    Tempo,
  };

  std::uint32_t tick = 0;
  Kind kind = Kind::NoteOn;
  std::uint8_t channel = 0;
  // The key of a note or the number of a controller.
  std::uint8_t key = 0;
  // The velocity, the control value, the program, the bend (0–16383, 8192 centred) or the tempo in
  // microseconds per quarter note.
  std::uint32_t value = 0;
};

// The events of a standard MIDI file, merged into one list by tick.
struct Song {
  // Ticks per quarter note.
  std::uint16_t division = 0;
  std::vector<SongEvent> events;
  // The ticks of the loop markers ("loopStart", "loopEnd"), when the song has them.
  std::optional<std::uint32_t> loopStart;
  std::optional<std::uint32_t> loopEnd;
  // The tick of the last end of track.
  std::uint32_t endTick = 0;
};

// Reads a standard MIDI file of format 0 or 1 with a division in ticks per quarter note. System
// exclusive events, aftertouch and the meta events besides tempo, markers and the end of track are
// skipped.
core::Result<Song> readSong(std::span<const std::byte> midi);

// The songs of a music font as font_meta.yaml lists them.
struct MusicTrack {
  std::uint32_t track = 0;
  // The file name in the font's folder, as dwx wrote it (track_<nn>.mid).
  std::string file;
  // `plays: once` stops at the end; `forever` loops (sound.c: seqPlay).
  bool loops = true;
};

struct MusicFont {
  std::vector<MusicTrack> tracks;

  [[nodiscard]] const MusicTrack* track(std::uint32_t number) const;
};

// Reads a font_meta.yaml. Keys the player does not need are left alone.
core::Result<MusicFont> readMusicFont(std::string_view yaml);

} // namespace dwe::assets
