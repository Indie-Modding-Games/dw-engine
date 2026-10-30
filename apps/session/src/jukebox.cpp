#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/music.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/jukebox.hpp>
#include <dwe/session/story_adapter.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <map>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view LinesSectionKey = "lines_section";
constexpr std::string_view LinesKey = "lines";
constexpr std::string_view GreetingKey = "greeting";
constexpr std::string_view AskKey = "ask";
constexpr std::string_view ChosenKey = "chosen";
constexpr std::string_view FarewellKey = "farewell";
constexpr std::string_view UnlockTriggerKey = "unlock_trigger";
constexpr std::string_view LockedTracksKey = "locked_tracks";
constexpr std::string_view ChosenVariableKey = "chosen_variable";
constexpr std::string_view NoTrackKey = "no_track";

constexpr std::string_view MusicTableId = "data/music";
// sound.c: playMusic, the fonts on the disc; a pack without bgm_tracks' fonts names the music
// menu's entries in their fonts' font_meta (used_by.jukebox).
constexpr std::int64_t FirstFont = 1;
constexpr std::int64_t LastFont = 0x21;

std::int32_t inRange(YamlReader& reader, const YamlNode& parent, std::string_view key,
                     std::int32_t most = std::numeric_limits<std::int32_t>::max()) {
  const YamlNode& node = reader.field(parent, key);
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && (value < 0 || value > most)) {
    reader.fail(node, std::format("{} must be from 0 to {}", key, most));
  }
  return value;
}

core::Result<void> checkJukeboxSection(const ruleset::Section& section) {
  if (auto read = readJukeboxRules(section); !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

std::optional<std::string> readText(const assets::VirtualFileSystem& files, std::string id) {
  auto parsed = assets::AssetId::parse(id);
  if (!parsed) {
    return std::nullopt;
  }
  auto bytes = files.read(*parsed);
  if (!bytes) {
    return std::nullopt;
  }
  return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

} // namespace

core::Result<JukeboxRules> readJukeboxRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, JukeboxSection,
                         {LinesSectionKey, LinesKey, UnlockTriggerKey, LockedTracksKey,
                          ChosenVariableKey, NoTrackKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  constexpr std::int32_t ByteMost = 0xFF;
  const YamlNode& lines = reader.field(root, LinesKey);
  if (reader.ok() &&
      !reader.expectKeys(lines, LinesKey, {GreetingKey, AskKey, ChosenKey, FarewellKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  JukeboxRules rules{.linesSection = inRange(reader, root, LinesSectionKey),
                     .lines = {.greeting = inRange(reader, lines, GreetingKey),
                               .ask = inRange(reader, lines, AskKey),
                               .chosen = inRange(reader, lines, ChosenKey),
                               .farewell = inRange(reader, lines, FarewellKey)},
                     .unlockTrigger = inRange(reader, root, UnlockTriggerKey),
                     .lockedTracks = {},
                     .chosenVariable = inRange(reader, root, ChosenVariableKey, ByteMost),
                     .noTrack = inRange(reader, root, NoTrackKey, ByteMost)};
  for (const YamlNode& track :
       reader.sequence(reader.field(root, LockedTracksKey), LockedTracksKey)) {
    const auto value = reader.integer<std::int32_t>(track);
    if (reader.ok() && (value < 0 || value >= rules.noTrack)) {
      reader.fail(track,
                  std::format("{} must be from 0 to {}", LockedTracksKey, rules.noTrack - 1));
    }
    rules.lockedTracks.push_back(value);
  }
  return reader.finish(std::move(rules), section.file);
}

bool JukeboxListView::up() {
  if (index() == 0) {
    return false;
  }
  if (cursor == 0) {
    --top;
  } else {
    --cursor;
  }
  return true;
}

bool JukeboxListView::down(std::int32_t count, std::int32_t visible) {
  if (index() + 1 >= count) {
    return false;
  }
  if (cursor + 1 == visible) {
    ++top;
  } else {
    ++cursor;
  }
  return true;
}

void JukeboxListView::toTop() {
  top = 0;
  cursor = 0;
}

// itemMenuCursorMoveToBottom.
void JukeboxListView::toBottom(std::int32_t count, std::int32_t visible) {
  const std::int32_t past = count - visible;
  top = std::max(past, 0);
  cursor = past <= 0 ? std::max(count - 1, 0) : visible - 1;
}

Jukebox::Jukebox(JukeboxDesc desc) : desc_(desc) {}

Jukebox::~Jukebox() = default;

ruleset::SectionChecks Jukebox::sectionChecks() const {
  return {{std::string{JukeboxSection}, checkJukeboxSection}};
}

core::Result<void> Jukebox::readRules(const ruleset::Ruleset& rules) {
  const ruleset::Section* section = rules.section(JukeboxSection);
  if (section == nullptr) {
    rules_.reset();
    return {};
  }
  auto read = readJukeboxRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> Jukebox::startNewGame(GameSession& /*session*/) {
  music_ = nullptr;
  rows_.clear();
  played_.reset();
  return {};
}

const JukeboxRules& Jukebox::rules() const {
  DWE_ASSERT(rules_.has_value());
  return *rules_;
}

// data/music's bgm_tracks, with the font and song of each entry; a pack extracted before they were
// written finds them in the fonts' font_meta (used_by.jukebox).
void Jukebox::readSongs() {
  std::vector<std::optional<MusicSong>> songs;
  if (desc_.files == nullptr) {
    songs_ = std::move(songs);
    return;
  }
  const std::optional<std::string> yaml = readText(*desc_.files, std::string{MusicTableId});
  if (!yaml) {
    core::logWarning("[port:jukebox] jukeboxFillTitleList: the packs have no {}", MusicTableId);
    songs_ = std::move(songs);
    return;
  }
  auto table = assets::readMusicTable(*yaml);
  if (!table) {
    core::logWarning("[port:jukebox] jukeboxFillTitleList: {}", table.error().message);
    songs_ = std::move(songs);
    return;
  }
  std::map<std::string, std::size_t, std::less<>> missing;
  for (const assets::BgmTrack& entry : table->bgmTracks) {
    if (entry.font && entry.track) {
      songs.push_back(MusicSong{.font = *entry.font, .track = *entry.track});
    } else {
      missing.emplace(entry.id, songs.size());
      songs.emplace_back();
    }
  }
  for (std::int64_t font = FirstFont; font <= LastFont && !missing.empty(); ++font) {
    const auto meta = readText(*desc_.files, std::format("music/{:03}/font_meta", font));
    if (!meta) {
      continue;
    }
    auto read = assets::readMusicFont(*meta);
    if (!read) {
      continue;
    }
    for (const assets::MusicTrack& track : read->tracks) {
      for (const std::string& id : track.jukebox) {
        if (const auto found = missing.find(id); found != missing.end()) {
          songs[found->second] = MusicSong{.font = font, .track = track.track};
          missing.erase(found);
        }
      }
    }
  }
  core::logDebug("[port:jukebox] MAIN_D_801303B8: {} songs, {} without a font", songs.size(),
                 missing.size());
  songs_ = std::move(songs);
}

std::optional<MusicSong> Jukebox::songOf(std::int32_t track) const {
  if (!songs_ || track < 0 || static_cast<std::size_t>(track) >= songs_->size()) {
    return std::nullopt;
  }
  return (*songs_)[static_cast<std::size_t>(track)];
}

bool Jukebox::open(JukeboxMusic& music) {
  if (!rules_ || desc_.story == nullptr) {
    core::logDebug("[port:jukebox] openJukebox (script_menu.c:947): no jukebox section or story; "
                   "the routine closes at once");
    return false;
  }
  if (!songs_) {
    readSongs();
  }
  if (!lineTables_ && desc_.files != nullptr) {
    if (auto tables = readMapHeadLineTables(*desc_.files); tables) {
      lineTables_ = std::move(*tables);
    } else {
      core::logWarning("[port:jukebox] resolveMapHeadEntry: {}", tables.error().message);
    }
  }
  // jukeboxFillTitleList: every entry, but the locked ones until the unlock trigger is set.
  const auto unlocked =
      desc_.story->state().trigger(static_cast<std::int32_t>(rules_->unlockTrigger));
  const bool all = unlocked && *unlocked;
  rows_.clear();
  const auto count = static_cast<std::int32_t>(songs_->size());
  for (std::int32_t track = 0; track < count && track < rules_->noTrack; ++track) {
    if (all || !std::ranges::contains(rules_->lockedTracks, track)) {
      rows_.push_back(track);
    }
  }
  if (rows_.empty()) {
    core::logDebug("[port:jukebox] jukeboxFillTitleList (script_menu.c:1636): no songs");
    return false;
  }
  music_ = &music;
  // The row whose song is the map's goes to the chosen variable; none stops the music. The variable
  // gets the row's index, the song's number while the list leaves none out before it.
  const std::optional<MusicSong> active = music.activeSong();
  std::optional<std::int32_t> playing;
  for (std::size_t row = 0; row < rows_.size() && active; ++row) {
    if (songOf(rows_[row]) == active) {
      playing = static_cast<std::int32_t>(row);
      break;
    }
  }
  played_ = playing;
  setChosen(playing.value_or(static_cast<std::int32_t>(rules_->noTrack)));
  if (!playing) {
    music.stopSong();
  }
  core::logDebug("[port:jukebox] jukeboxFillTitleList (script_menu.c:1636): {} songs{}, "
                 "playing {}",
                 rows_.size(), all ? " (all unlocked)" : "",
                 playing ? std::to_string(*playing) : std::string{"none"});
  return true;
}

std::optional<std::int32_t> Jukebox::chosenTrack() const {
  if (!rules_ || desc_.story == nullptr) {
    return std::nullopt;
  }
  const auto value =
      desc_.story->state().variable(static_cast<std::int32_t>(rules_->chosenVariable));
  if (!value || *value == rules_->noTrack) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(*value);
}

JukeboxListView Jukebox::startView(std::int32_t visible) const {
  // createJukeboxMenu: the row of the chosen song, the first one when none; at the window's bottom
  // when it is past the first window.
  const std::optional<std::int32_t> chosen = chosenTrack();
  const auto found = chosen ? std::ranges::find(rows_, *chosen) : rows_.end();
  const std::int32_t row =
      found == rows_.end() ? 0 : static_cast<std::int32_t>(found - rows_.begin());
  if (row - visible < 0) {
    return {.top = 0, .cursor = row};
  }
  return {.top = row - visible + 1, .cursor = visible - 1};
}

bool Jukebox::choose(std::int32_t row) {
  if (!isOpen() || row < 0 || static_cast<std::size_t>(row) >= rows_.size()) {
    return false;
  }
  const std::int32_t track = rows_[static_cast<std::size_t>(row)];
  core::logDebug("[port:jukebox] tickJukeboxMenu (script_menu.c:2396): confirm, song {}", track);
  return setChosen(track);
}

bool Jukebox::playChosen() {
  if (!isOpen()) {
    return false;
  }
  const std::optional<std::int32_t> chosen = chosenTrack();
  if (chosen == played_) {
    return false;
  }
  played_ = chosen;
  // tickJukeboxMenu: stopBGM, then playMusic of MAIN_D_801303B8[song].
  music_->stopSong();
  const std::optional<MusicSong> song = chosen ? songOf(*chosen) : std::nullopt;
  if (!song) {
    return false;
  }
  core::logDebug("[port:jukebox] tickJukeboxMenu (script_menu.c:2421): playMusic({}, {})",
                 song->font, song->track);
  music_->playSong(*song);
  return true;
}

void Jukebox::close() {
  if (!isOpen()) {
    return;
  }
  core::logDebug(
      "[port:jukebox] openJukebox (script_menu.c:964): state 2, playBGM(ACTIVE_BGM_FONT)");
  music_->replayActive();
  music_ = nullptr;
}

bool Jukebox::setChosen(std::int64_t value) {
  if (auto set = desc_.story->state().setVariable(static_cast<std::int32_t>(rules_->chosenVariable),
                                                  static_cast<std::uint8_t>(value));
      !set) {
    core::logWarning("[port:jukebox] writePStat(0xf9): {}", set.error().message);
    return false;
  }
  return true;
}

std::string Jukebox::lineKey(std::int32_t line) const {
  if (!lineTables_ || !rules_) {
    return {};
  }
  const auto table = lineTables_->find(rules_->linesSection);
  const auto at = static_cast<std::size_t>(line);
  if (table == lineTables_->end() || line < 0 || at >= table->second.size()) {
    return {};
  }
  return table->second[at];
}

} // namespace dwe::session
