#pragma once

#include <dwe/assets/music.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/audio/sound_font.hpp>
#include <dwe/game/story/story_ports.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <utility>

namespace dwe::session {

// What the music reads from the rest of the game.
struct MusicFacts {
  // The story's music mode, variable 245 (script_interp.c: handleMusicOverride).
  std::function<std::int64_t()> mode;
  // The hour of the game clock.
  std::function<std::int32_t()> hour;
};

// The original's background music on the game's audio (docs/architecture/audio.md, "Música
// (tramo 3)"): the story's set_bgm and stop_bgm pick a font, the music mode and the hour its song,
// and music/<font>/ holds the SoundFont, the songs and font_meta.yaml. A font is loaded when its
// song first plays and kept until another one plays. A font that does not load warns once.
class SessionMusic final : public game::story::MusicPort {
public:
  // The audio and the file system must outlive the session.
  SessionMusic(audio::Audio& audio, const assets::VirtualFileSystem& files, MusicFacts facts);

  void playBgm(std::int64_t font) override;
  void resetBgm() override;

private:
  struct LoadedFont {
    std::int64_t font = 0;
    std::shared_ptr<audio::SoundFont> synth;
    assets::MusicFont meta;
  };

  [[nodiscard]] std::pair<std::int64_t, std::int64_t> overridden(std::int64_t font) const;
  void stopBgm();
  void playMusic(std::int64_t font, std::int64_t track);
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
};

} // namespace dwe::session
