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
  // used_by.jukebox: the ids of the music menu's entries (data/music.yaml bgm_tracks) that play
  // this song.
  std::vector<std::string> jukebox;
};

struct MusicFont {
  std::vector<MusicTrack> tracks;

  [[nodiscard]] const MusicTrack* track(std::uint32_t number) const;
};

// Reads a font_meta.yaml. Keys the player does not need are left alone.
core::Result<MusicFont> readMusicFont(std::string_view yaml);

// A music mode of the story's variable 245 that plays a fixed font and song, whatever font the map
// asked for (script_interp.c: handleMusicOverride).
struct FieldMusicOverride {
  std::int64_t mode = 0;
  std::int64_t font = 0;
  std::int64_t track = 0;
};

// Which song of the battle font a battle plays (btl.c: loadBattleData).
struct BattleMusic {
  std::int64_t font = 0;
  // The species id of the first enemy that plays bossTrack.
  std::string boss;
  std::int64_t bossTrack = 0;
  std::int64_t scriptedTrack = 0;
  std::int64_t defaultTrack = 0;
  // Enemy types from firstEnemyType up to before endEnemyType play
  // enemyMusicMapping[type - firstEnemyType].
  std::int64_t firstEnemyType = 0;
  std::int64_t endEnemyType = 0;
};

// An entry of the music menu (BGM_TRACK_NAMES, shop/script_menu.c: jukeboxFillTitleList): its id,
// and the font and song it plays (MAIN_D_801303B8), which a pack extracted before they were written
// lacks.
struct BgmTrack {
  std::string id;
  std::optional<std::int64_t> font;
  std::optional<std::int64_t> track;
};

// What the game reads of data/music.yaml (content/schemas/music.schema.json). A pack extracted
// before the music modes and the battle music were written has none of them.
struct MusicTable {
  // In the order of their index.
  std::vector<BgmTrack> bgmTracks;
  std::vector<std::int64_t> enemyMusicMapping;
  std::vector<FieldMusicOverride> fieldOverrides;
  std::optional<BattleMusic> battle;

  [[nodiscard]] const FieldMusicOverride* overrideOf(std::int64_t mode) const;
};

// Reads data/music.yaml.
core::Result<MusicTable> readMusicTable(std::string_view yaml);

} // namespace dwe::assets
