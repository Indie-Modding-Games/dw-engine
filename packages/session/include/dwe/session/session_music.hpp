#pragma once

#include <dwe/assets/music.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/audio/sound_font.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/session/jukebox.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace dwe::session {

// What the music reads from the rest of the game.
struct MusicFacts {
  // The story's music mode, variable 245 (script_interp.c: handleMusicOverride).
  std::function<std::int64_t()> mode;
  // The hour of the game clock.
  std::function<std::int32_t()> hour;
};

// The first enemy of a battle and how the battle started, for its song (btl.c: loadBattleData).
struct BattleMusicFacts {
  // The species id of the first enemy, and its type in the original's Digimon table.
  std::string firstEnemy;
  std::optional<std::int64_t> firstEnemyType;
  bool scripted = false;
};

// The original's background music on the game's audio (docs/architecture/audio.md, "Música
// (tramo 3)"): the story's set_bgm and stop_bgm pick a font, the music mode and the hour its song,
// and music/<font>/ holds the SoundFont, the songs and font_meta.yaml. data/music.yaml gives the
// fixed songs of the music modes and the battle's songs. A font is loaded when its song first plays
// and kept until another one plays. A font or a data/music.yaml that does not load warns once.
class SessionMusic final : public game::story::MusicPort, public JukeboxMusic {
public:
  // The audio and the file system must outlive the session.
  SessionMusic(audio::Audio& audio, const assets::VirtualFileSystem& files, MusicFacts facts);
  // main.c: main, cleanupGame then finalizeMusic: the game's song ends with the game, so the title
  // screen it goes back to (or the ending's movie) is silent, as the original's.
  ~SessionMusic() override;
  SessionMusic(const SessionMusic&) = delete;
  SessionMusic& operator=(const SessionMusic&) = delete;
  SessionMusic(SessionMusic&&) = delete;
  SessionMusic& operator=(SessionMusic&&) = delete;

  void playBgm(std::int64_t font) override;
  void resetBgm() override;
  // map.c: changeMap and the head's set_script (script_interp.c: scriptInstructionFBtoFF,
  // resetBGM): the original cuts the music on every map and the head's set_bgm plays the map's song
  // from its start. With continuous music the song playing goes on until the head picks one.
  void enterMap() override;
  // A head that picked no song leaves the map silent, as in the original.
  void mapHeadDone() override;

  // Continuous music (an engine improvement, the option continuous_music): a map whose song is the
  // one playing lets it go on instead of playing it again from its start; a map with another song
  // or none still changes it at once. Off, every map plays its song from the start, as the
  // original does.
  void setContinuousMusic(bool on) { continuous_ = on; }
  [[nodiscard]] bool continuousMusic() const { return continuous_; }

  // script_interp.c: updateBGM, each tick of the game clock: the song again when the mode or the
  // hour picks another one. Nothing during a battle.
  void updateBgm();
  // script_interp.c: START_BATTLE and btl.c: loadBattleData: stops the field's music and plays the
  // battle's song.
  void startBattle(const BattleMusicFacts& facts);
  // battle_main.c: BTL_handleVictorySequence and BTL_deinitializeCombat: the battle's song stops.
  void endBattle();
  // tamer.c: startBattle, back on the field: a victory plays the field's song again from the start;
  // after a defeat or an escape the music stays stopped.
  void leaveBattle(bool won);

  // main.c: main, around playMovie: finalizeMusic stops the music before a movie and
  // initializeMusic lets it play again after it. While held, nothing the game asks plays, so a
  // movie over a loaded field is silent but for its own sound; on release the song the story
  // asked for meanwhile starts from its beginning, as the field's head plays it after the movie in
  // the original.
  void holdForMovie();
  void releaseAfterMovie();
  [[nodiscard]] bool heldForMovie() const { return held_; }

  // The jukebox's (shop/script_menu.c: jukeboxFillTitleList, tickJukeboxMenu, openJukebox).
  [[nodiscard]] std::optional<MusicSong> activeSong() const override;
  void playSong(MusicSong song) override;
  void stopSong() override;
  void replayActive() override;

private:
  struct LoadedFont {
    std::int64_t font = 0;
    std::shared_ptr<audio::SoundFont> synth;
    assets::MusicFont meta;
  };

  [[nodiscard]] std::pair<std::int64_t, std::int64_t> overridden(std::int64_t font);
  [[nodiscard]] std::int64_t battleTrack(const assets::BattleMusic& battle,
                                         const BattleMusicFacts& facts) const;
  const assets::MusicTable* table();
  void stopBgm();
  void playMusic(std::int64_t font, std::int64_t track);
  // Whether a font's song is the one playing (seqOpen plays the first song for one past the last).
  [[nodiscard]] bool playing(std::int64_t font, std::int64_t track) const;
  const LoadedFont* load(std::int64_t font);

  audio::Audio* audio_;
  const assets::VirtualFileSystem* files_;
  MusicFacts facts_;
  // script_interp.c: ACTIVE_BGM_FONT and ACTIVE_BGM_TRACK, what the story asked for.
  std::optional<std::int64_t> activeFont_;
  std::int64_t activeTrack_ = 0;
  // sound.c: CURRENT_SEQ_TRACK, the song playing; none after a stop.
  std::optional<std::int64_t> currentTrack_;
  std::optional<LoadedFont> loaded_;
  std::set<std::int64_t> failedFonts_;
  // data/music.yaml, read on first use.
  std::optional<assets::MusicTable> table_;
  bool tableRead_ = false;
  // tamer.c: GAME_STATE: the clock does not update the music during a battle.
  bool battling_ = false;
  // The option continuous_music, and whether a map head runs with the song before it still on.
  bool continuous_ = false;
  bool carrying_ = false;
  // Whether a movie holds the music (holdForMovie).
  bool held_ = false;
};

} // namespace dwe::session
