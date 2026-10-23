#include <dwe/assets/asset_id.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/session_music.hpp>

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::session {

namespace {

// script_interp.c: playBGM, the font that stops the music.
constexpr std::int64_t StopFont = 0xFF;
// sound.c: playMusic, the fonts on the disc.
constexpr std::int64_t FirstFont = 1;
constexpr std::int64_t LastFont = 0x21;
// sound.c: seqPlay, SsSeqSetVol(.., 0x50, 0x50) of a full 0x7F.
constexpr float SequenceVolume = 80.0F / 127.0F;
// script_interp.c: handleMusicOverride, mode 0: the day song from 6:00 to 20:59, the night one
// otherwise.
constexpr std::int64_t DayNightMode = 0;
constexpr std::int64_t DaySongMode = 1;
constexpr std::int32_t DayStart = 6;
constexpr std::int32_t NightStart = 21;
constexpr std::string_view MusicTableId = "data/music";

core::Result<std::vector<std::byte>> readAsset(const assets::VirtualFileSystem& files,
                                               const std::string& id) {
  auto parsed = assets::AssetId::parse(id);
  if (!parsed) {
    return std::unexpected{parsed.error()};
  }
  return files.read(*parsed);
}

std::string folderOf(std::int64_t font) {
  return std::format("music/{:03}", font);
}

} // namespace

SessionMusic::SessionMusic(audio::Audio& audio, const assets::VirtualFileSystem& files,
                           MusicFacts facts)
    : audio_(&audio), files_(&files), facts_(std::move(facts)) {}

void SessionMusic::playBgm(std::int64_t font) {
  core::logDebug("[port:audio] playBGM (script_interp.c:2587): font={}", font);
  if (font == StopFont) {
    resetBgm();
    return;
  }
  const auto [chosen, track] = overridden(font);
  core::logDebug("[port:audio] handleMusicOverride (script_interp.c:1584): font={} -> font={} "
                 "track={}",
                 font, chosen, track);
  activeFont_ = chosen;
  activeTrack_ = track;
  stopBgm();
  playMusic(chosen, track);
}

void SessionMusic::resetBgm() {
  core::logDebug("[port:audio] resetBGM (script_interp.c:2377)");
  activeFont_.reset();
  stopBgm();
}

// script_interp.c: handleMusicOverride. Mode 0 picks the day or the night song of the font, mode 1
// the first one, and the modes of data/music.yaml's field_overrides a fixed font and song. Any
// other mode leaves the song unset in the original; here it plays the font's first song.
std::pair<std::int64_t, std::int64_t> SessionMusic::overridden(std::int64_t font) {
  const std::int64_t mode = facts_.mode ? facts_.mode() : DayNightMode;
  if (mode == DayNightMode) {
    const std::int32_t hour = facts_.hour ? facts_.hour() : DayStart;
    return {font, hour >= DayStart && hour < NightStart ? 0 : 1};
  }
  if (mode == DaySongMode) {
    return {font, 0};
  }
  if (const assets::MusicTable* music = table(); music != nullptr) {
    if (const assets::FieldMusicOverride* fixed = music->overrideOf(mode); fixed != nullptr) {
      return {fixed->font, fixed->track};
    }
  }
  return {font, 0};
}

void SessionMusic::updateBgm() {
  if (battling_) {
    return;
  }
  if (!activeFont_) {
    if (currentTrack_) {
      resetBgm();
    }
    return;
  }
  const std::int64_t active = *activeFont_;
  const auto [font, track] = overridden(active);
  if (font == active && track == activeTrack_) {
    return;
  }
  core::logDebug("[port:audio] updateBGM (script_interp.c:2607): font={} track={} -> font={} "
                 "track={}",
                 active, activeTrack_, font, track);
  activeFont_ = font;
  activeTrack_ = track;
  stopBgm();
  playMusic(font, track);
}

void SessionMusic::startBattle(const BattleMusicFacts& facts) {
  battling_ = true;
  core::logDebug("[port:audio] scriptInstruction64to7E START_BATTLE (script_interp.c:1913): "
                 "stopBGM");
  stopBgm();
  const assets::MusicTable* music = table();
  if (music == nullptr || !music->battle) {
    core::logDebug("[port:audio] loadBattleData (btl.c:346): {} has no battle music", MusicTableId);
    return;
  }
  const std::int64_t track = battleTrack(*music->battle, facts);
  core::logDebug("[port:audio] loadBattleData (btl.c:346): enemy={} scripted={} -> font={} "
                 "track={}",
                 facts.firstEnemy, facts.scripted, music->battle->font, track);
  playMusic(music->battle->font, track);
}

// btl.c: loadBattleData. The original checks the type of the first enemy for the boss and of the
// enemy that started the battle for the rest; here both are the first enemy.
std::int64_t SessionMusic::battleTrack(const assets::BattleMusic& battle,
                                       const BattleMusicFacts& facts) const {
  if (facts.firstEnemy == battle.boss) {
    return battle.bossTrack;
  }
  if (facts.scripted) {
    return battle.scriptedTrack;
  }
  const std::optional<std::int64_t> type = facts.firstEnemyType;
  if (type && *type >= battle.firstEnemyType && *type < battle.endEnemyType && table_) {
    const auto index = static_cast<std::size_t>(*type - battle.firstEnemyType);
    if (index < table_->enemyMusicMapping.size()) {
      return table_->enemyMusicMapping[index];
    }
  }
  return battle.defaultTrack;
}

void SessionMusic::endBattle() {
  core::logDebug("[port:audio] BTL_deinitializeCombat (battle_main.c:814): stopBGM");
  stopBgm();
}

void SessionMusic::leaveBattle(bool won) {
  battling_ = false;
  if (!won) {
    return;
  }
  core::logDebug("[port:audio] startBattle (tamer.c:862): playBGM(ACTIVE_BGM_FONT)");
  if (activeFont_) {
    playBgm(*activeFont_);
  } else {
    resetBgm();
  }
}

// data/music.yaml, read once; a table that does not read warns once and leaves the music modes and
// the battle without their songs.
const assets::MusicTable* SessionMusic::table() {
  if (!std::exchange(tableRead_, true)) {
    auto read = [&]() -> core::Result<assets::MusicTable> {
      auto bytes = readAsset(*files_, std::string{MusicTableId});
      if (!bytes) {
        return std::unexpected{bytes.error()};
      }
      return assets::readMusicTable(
          std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    }();
    if (read) {
      table_ = std::move(*read);
    } else {
      core::logWarning("{}", core::formatMessage(messages::MusicTableFailed, MusicTableId,
                                                 read.error().message));
    }
  }
  return table_ ? &*table_ : nullptr;
}

// sound.c: stopBGM.
void SessionMusic::stopBgm() {
  core::logDebug("[port:audio] stopBGM (sound.c:779)");
  audio_->stopMusic();
  currentTrack_.reset();
}

// sound.c: playMusic, seqOpen and seqPlay. The font stays loaded across songs.
void SessionMusic::playMusic(std::int64_t font, std::int64_t track) {
  if (font < FirstFont || font > LastFont) {
    core::logDebug("[port:audio] playMusic (sound.c:753): font {} is not a music font", font);
    return;
  }
  if (loaded_ && loaded_->font == font && currentTrack_ == track) {
    return;
  }
  const LoadedFont* current = load(font);
  if (current == nullptr) {
    return;
  }
  // seqOpen: a song past the font's last plays its first one.
  const auto* song = track >= 0 ? current->meta.track(static_cast<std::uint32_t>(track)) : nullptr;
  if (song == nullptr) {
    track = 0;
    song = current->meta.track(0);
  }
  currentTrack_ = track;
  const std::string folder = folderOf(font);
  if (song == nullptr) {
    core::logWarning("{}", core::formatMessage(messages::MusicSongFailed, folder, track,
                                               "font_meta.yaml lists no songs"));
    return;
  }
  const std::string file = song->file.substr(0, song->file.rfind('.'));
  auto bytes = readAsset(*files_, std::format("{}/{}", folder, file));
  if (!bytes) {
    core::logWarning(
        "{}", core::formatMessage(messages::MusicSongFailed, folder, track, bytes.error().message));
    return;
  }
  auto read = assets::readSong(*bytes);
  if (!read) {
    core::logWarning(
        "{}", core::formatMessage(messages::MusicSongFailed, folder, track, read.error().message));
    return;
  }
  core::logDebug("[port:audio] seqPlay (sound.c:488): font={} track={} {} events={}", font, track,
                 song->loops ? "forever" : "once", read->events.size());
  if (auto played =
          audio_->playMusic({.font = current->synth,
                             .song = std::make_shared<const assets::Song>(std::move(*read)),
                             .loops = song->loops,
                             .gain = SequenceVolume});
      !played) {
    core::logWarning("{}", core::formatMessage(messages::MusicSongFailed, folder, track,
                                               played.error().message));
  }
}

// sound.c: loadMusicFont. The font before is let go; the mixer keeps it until the new song starts.
const SessionMusic::LoadedFont* SessionMusic::load(std::int64_t font) {
  if (loaded_ && loaded_->font == font) {
    return &*loaded_;
  }
  loaded_.reset();
  if (failedFonts_.contains(font)) {
    return nullptr;
  }
  const std::string folder = folderOf(font);
  core::logDebug("[port:audio] loadMusicFont (sound.c:738): {}", folder);
  const auto fail = [&](const std::string& why) -> const LoadedFont* {
    core::logWarning("{}", core::formatMessage(messages::MusicFontFailed, folder, why));
    failedFonts_.insert(font);
    return nullptr;
  };
  auto metaBytes = readAsset(*files_, folder + "/font_meta");
  if (!metaBytes) {
    return fail(metaBytes.error().message);
  }
  auto meta = assets::readMusicFont(
      std::string_view{reinterpret_cast<const char*>(metaBytes->data()), metaBytes->size()});
  if (!meta) {
    return fail(meta.error().message);
  }
  auto sf2 = readAsset(*files_, folder + "/font");
  if (!sf2) {
    return fail(sf2.error().message);
  }
  auto synth = audio::SoundFont::load(*sf2);
  if (!synth) {
    return fail(synth.error().message);
  }
  loaded_ = LoadedFont{.font = font, .synth = std::move(*synth), .meta = std::move(*meta)};
  return &*loaded_;
}

} // namespace dwe::session
