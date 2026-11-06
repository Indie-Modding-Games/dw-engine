#pragma once

#include <dwe/core/error.hpp>
#include <dwe/session/map_head_lines.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::session {

class StoryAdapter;

// The ruleset section of the jukebox (rules/<id>/jukebox.yaml).
inline constexpr std::string_view JukeboxSection = "jukebox";

// What the jukebox's owner says, as line numbers of its MAPHEAD section (shop/script_menu.c:
// openJukebox and tickJukeboxMenu): the greeting before the list, the question over it, the answer
// to a pick and the farewell.
struct JukeboxLines {
  std::int32_t greeting = 0;
  std::int32_t ask = 0;
  std::int32_t chosen = 0;
  std::int32_t farewell = 0;
};

struct JukeboxRules {
  // showMapHeadTextbox(..., 0x4d6): the MAPHEAD section of the owner's lines.
  std::int32_t linesSection = 0;
  JukeboxLines lines;
  // jukeboxFillTitleList: the songs the list leaves out until this trigger is set.
  std::int64_t unlockTrigger = 0;
  std::vector<std::int32_t> lockedTracks;
  // The variable that holds the chosen song (writePStat(0xf9)), and the number that says none.
  std::int64_t chosenVariable = 0;
  std::int64_t noTrack = 0;
};

core::Result<JukeboxRules> readJukeboxRules(const ruleset::Section& section);

// A song of the music: a music font and a song of it (sound.c: playMusic).
struct MusicSong {
  std::int64_t font = 0;
  std::int64_t track = 0;

  friend bool operator==(const MusicSong&, const MusicSong&) = default;
};

// What the jukebox needs of the music (SessionMusic).
class JukeboxMusic {
public:
  JukeboxMusic() = default;
  JukeboxMusic(const JukeboxMusic&) = delete;
  JukeboxMusic& operator=(const JukeboxMusic&) = delete;
  JukeboxMusic(JukeboxMusic&&) = delete;
  JukeboxMusic& operator=(JukeboxMusic&&) = delete;
  virtual ~JukeboxMusic() = default;

  // getActiveBGMFont and getActiveBGMVariant: the song the map's script asked for, if any.
  [[nodiscard]] virtual std::optional<MusicSong> activeSong() const = 0;
  // stopBGM, then playMusic(font, track): the song plays and the map's song stays the active one.
  virtual void playSong(MusicSong song) = 0;
  // stopBGM.
  virtual void stopSong() = 0;
  // playBGM(ACTIVE_BGM_FONT): the map's song again, from its start.
  virtual void replayActive() = 0;
};

// The list's first row shown and the cursor among the rows shown (ItemMenuBox topRow, cursor).
struct JukeboxListView {
  std::int32_t top = 0;
  std::int32_t cursor = 0;

  [[nodiscard]] std::int32_t index() const { return top + cursor; }
  // itemMenuCursorUp and itemMenuCursorDown: false when the cursor is at the end and stays (the
  // original's bump sound, 0xb); the list scrolls under it at the window's edges.
  bool up();
  bool down(std::int32_t count, std::int32_t visible);
  // itemMenuCursorTop and itemMenuCursorBottom (R1 held): the first row or the last one.
  void toTop();
  void toBottom(std::int32_t count, std::int32_t visible);
};

struct JukeboxDesc {
  // The mounted packs, for data/music, music/<font>/font_meta and script/maphead; they must outlive
  // the jukebox.
  const assets::VirtualFileSystem* files = nullptr;
  // The story's trigger and variable; it must outlive the jukebox.
  StoryAdapter* story = nullptr;
};

// The jukebox the story opens (script_interp.c: CALL_ROUTINE 9 -> shop/script_menu.c: openJukebox,
// jukeboxFillTitleList, createJukeboxMenu, tickJukeboxMenu): the music menu's songs, the one that
// plays marked; a pick plays its song over the map's, and leaving plays the map's song again. The
// story waits while it is open.
class Jukebox final : public SessionSystem {
public:
  explicit Jukebox(JukeboxDesc desc);
  ~Jukebox() override;
  Jukebox(const Jukebox&) = delete;
  Jukebox& operator=(const Jukebox&) = delete;
  Jukebox(Jukebox&&) = delete;
  Jukebox& operator=(Jukebox&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // A ruleset without the section has no jukebox: the routine then closes at once.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> startNewGame(GameSession& session) override;

  // openJukebox state 0 and jukeboxFillTitleList: lists the songs, marks the one the map plays in
  // the chosen variable, or none and stops the music when no row plays it. False (and closed)
  // without the section, the story or a song to list.
  bool open(JukeboxMusic& music);
  [[nodiscard]] bool isOpen() const { return music_ != nullptr; }
  [[nodiscard]] bool available() const { return rules_.has_value(); }
  [[nodiscard]] const JukeboxRules& rules() const;

  // The list's rows: the songs' numbers in the music menu (BGM_TRACK_NAMES), in their order.
  [[nodiscard]] const std::vector<std::int32_t>& rows() const { return rows_; }
  // The chosen variable: the song that plays, if any.
  [[nodiscard]] std::optional<std::int32_t> chosenTrack() const;
  // createJukeboxMenu: the list opens with the chosen song's row shown and under the cursor.
  [[nodiscard]] JukeboxListView startView(std::int32_t visible) const;
  // tickJukeboxMenu's confirm: the row's song goes to the chosen variable. False for a row past the
  // list.
  bool choose(std::int32_t row);
  // tickJukeboxMenu, once the answer's box is up: when the chosen song is not the one it played
  // last, stops the music and plays it. Whether a song started.
  bool playChosen();
  // openJukebox state 2: the map's song again, and the jukebox closes.
  void close();

  // The text key of an owner's line (resolveMapHeadEntry); empty when MAPHEAD's listing does not
  // have it.
  [[nodiscard]] std::string lineKey(std::int32_t line) const;

private:
  [[nodiscard]] std::optional<MusicSong> songOf(std::int32_t track) const;
  bool setChosen(std::int64_t value);
  void readSongs();

  JukeboxDesc desc_;
  std::optional<JukeboxRules> rules_;
  JukeboxMusic* music_ = nullptr;
  std::vector<std::int32_t> rows_;
  // MAIN_D_80134F8F: the song it played last, which the chosen variable is compared with.
  std::optional<std::int32_t> played_;
  // data/music's bgm_tracks: the song each entry of the music menu plays, by entry; read on the
  // first visit.
  std::optional<std::vector<std::optional<MusicSong>>> songs_;
  std::optional<MapHeadLineTables> lineTables_;
};

} // namespace dwe::session
