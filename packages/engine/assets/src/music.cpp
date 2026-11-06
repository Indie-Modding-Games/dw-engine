#include <dwe/assets/media_headers.hpp>
#include <dwe/assets/music.hpp>
#include <dwe/assets/yaml_reader.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace dwe::assets {

namespace {

using core::YamlNode;
using Kind = SongEvent::Kind;

constexpr std::string_view Context = "the music font";
constexpr std::string_view LoopStartMarker = "loopStart";
constexpr std::string_view LoopEndMarker = "loopEnd";
constexpr std::uint8_t MetaStatus = 0xFF;
constexpr std::uint8_t SysexStatus = 0xF0;
constexpr std::uint8_t SysexContinue = 0xF7;
constexpr std::uint8_t MetaMarker = 0x06;
constexpr std::uint8_t MetaEndOfTrack = 0x2F;
constexpr std::uint8_t MetaTempo = 0x51;
constexpr std::size_t ChunkHeader = 8;

// Reads one track chunk's body, byte by byte.
class TrackReader {
public:
  explicit TrackReader(std::span<const std::byte> body) : body_{body} {}

  [[nodiscard]] bool done() const { return at_ >= body_.size(); }
  [[nodiscard]] bool failed() const { return failed_; }

  std::uint8_t byte() {
    if (done()) {
      failed_ = true;
      return 0;
    }
    return std::to_integer<std::uint8_t>(body_[at_++]);
  }

  // A variable-length quantity: at most four bytes of seven bits.
  std::uint32_t quantity() {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const std::uint8_t next = byte();
      value = (value << 7U) | (next & 0x7FU);
      if ((next & 0x80U) == 0) {
        return value;
      }
    }
    failed_ = true;
    return value;
  }

  std::span<const std::byte> bytes(std::uint32_t count) {
    if (count > body_.size() - std::min(at_, body_.size())) {
      failed_ = true;
      at_ = body_.size();
      return {};
    }
    const auto taken = body_.subspan(at_, count);
    at_ += count;
    return taken;
  }

private:
  std::span<const std::byte> body_;
  std::size_t at_ = 0;
  bool failed_ = false;
};

std::uint32_t bigEndian(std::span<const std::byte> bytes) {
  std::uint32_t value = 0;
  for (const std::byte b : bytes) {
    value = (value << 8U) | std::to_integer<std::uint32_t>(b);
  }
  return value;
}

bool spells(std::span<const std::byte> bytes, std::string_view text) {
  return std::ranges::equal(bytes, text,
                            [](std::byte b, char c) { return b == static_cast<std::byte>(c); });
}

void readMeta(TrackReader& reader, std::uint32_t tick, Song& song, bool& ended) {
  const std::uint8_t type = reader.byte();
  const auto data = reader.bytes(reader.quantity());
  if (type == MetaTempo && data.size() == 3) {
    song.events.push_back({.tick = tick, .kind = Kind::Tempo, .value = bigEndian(data)});
  } else if (type == MetaMarker && spells(data, LoopStartMarker)) {
    song.loopStart = tick;
  } else if (type == MetaMarker && spells(data, LoopEndMarker)) {
    song.loopEnd = tick;
  } else if (type == MetaEndOfTrack) {
    ended = true;
  }
}

// One channel message from its first data byte on; aftertouch is read and dropped.
void readChannel(TrackReader& reader, std::uint8_t status, std::uint8_t first, std::uint32_t tick,
                 Song& song) {
  const auto channel = static_cast<std::uint8_t>(status & 0x0FU);
  const auto kind = status & 0xF0U;
  if (kind == 0xC0U || kind == 0xD0U) {
    if (kind == 0xC0U) {
      song.events.push_back(
          {.tick = tick, .kind = Kind::Program, .channel = channel, .value = first});
    }
    return;
  }
  const std::uint8_t second = reader.byte();
  switch (kind) {
  case 0x80U:
    song.events.push_back({.tick = tick, .kind = Kind::NoteOff, .channel = channel, .key = first});
    break;
  case 0x90U:
    song.events.push_back({.tick = tick,
                           .kind = second == 0 ? Kind::NoteOff : Kind::NoteOn,
                           .channel = channel,
                           .key = first,
                           .value = second});
    break;
  case 0xB0U:
    song.events.push_back(
        {.tick = tick, .kind = Kind::Control, .channel = channel, .key = first, .value = second});
    break;
  case 0xE0U:
    song.events.push_back({.tick = tick,
                           .kind = Kind::PitchBend,
                           .channel = channel,
                           .value = (std::uint32_t{second} << 7U) | first});
    break;
  default:
    break;
  }
}

core::Result<void> readTrack(std::span<const std::byte> body, std::size_t index, Song& song) {
  TrackReader reader{body};
  std::uint32_t tick = 0;
  std::uint8_t running = 0;
  bool ended = false;
  while (!reader.done() && !ended && !reader.failed()) {
    tick += reader.quantity();
    const std::uint8_t lead = reader.byte();
    if (lead == MetaStatus) {
      readMeta(reader, tick, song, ended);
      continue;
    }
    if (lead == SysexStatus || lead == SysexContinue) {
      static_cast<void>(reader.bytes(reader.quantity()));
      continue;
    }
    if ((lead & 0x80U) == 0) {
      // Running status: the byte read is the first data byte of a message like the last one.
      if (running == 0) {
        return core::fail(std::format("track {}: a data byte with no running status", index));
      }
      const std::uint8_t first = lead;
      readChannel(reader, running, first, tick, song);
      continue;
    }
    running = lead;
    readChannel(reader, running, reader.byte(), tick, song);
  }
  if (reader.failed()) {
    return core::fail(std::format("track {} ends inside an event", index));
  }
  song.endTick = std::max(song.endTick, tick);
  return {};
}

} // namespace

core::Result<Song> readSong(std::span<const std::byte> midi) {
  auto header = readMidiHeader(midi);
  if (!header) {
    return std::unexpected{header.error()};
  }
  if (header->format > 1 || (header->division & 0x8000U) != 0) {
    return core::fail(std::format("a MIDI file of format {} with division {:#x} is not supported",
                                  header->format, header->division));
  }
  Song song{.division = header->division,
            .events = {},
            .loopStart = std::nullopt,
            .loopEnd = std::nullopt,
            .endTick = 0};
  std::size_t offset = ChunkHeader + bigEndian(midi.subspan(4, 4));
  std::size_t track = 0;
  // readMidiHeader checked that every chunk fits.
  while (offset + ChunkHeader <= midi.size()) {
    const std::size_t size = bigEndian(midi.subspan(offset + 4, 4));
    if (spells(midi.subspan(offset, 4), "MTrk")) {
      if (auto read = readTrack(midi.subspan(offset + ChunkHeader, size), track++, song); !read) {
        return std::unexpected{read.error()};
      }
    }
    offset += ChunkHeader + size;
  }
  std::ranges::stable_sort(song.events, {}, &SongEvent::tick);
  return song;
}

const MusicTrack* MusicFont::track(std::uint32_t number) const {
  const auto found = std::ranges::find(tracks, number, &MusicTrack::track);
  return found == tracks.end() ? nullptr : &*found;
}

core::Result<MusicFont> readMusicFont(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
  }
  YamlReader reader;
  MusicFont font;
  for (const YamlNode& node : reader.sequence(reader.field(*tree, "tracks"), "tracks")) {
    MusicTrack track{.track = reader.integer<std::uint32_t>(reader.field(node, "track")),
                     .file = reader.text(reader.field(node, "file")),
                     .loops = true,
                     .jukebox = {}};
    const std::string plays = reader.text(reader.field(node, "plays"));
    if (reader.ok() && plays != "forever" && plays != "once") {
      reader.fail(node, "plays is forever or once");
    }
    track.loops = plays != "once";
    if (const YamlNode* usedBy = node.find("used_by"); usedBy != nullptr) {
      if (const YamlNode* jukebox = usedBy->find("jukebox"); jukebox != nullptr) {
        for (const YamlNode& id : reader.sequence(*jukebox, "used_by.jukebox")) {
          track.jukebox.push_back(reader.text(id));
        }
      }
    }
    if (reader.ok() && font.track(track.track) != nullptr) {
      reader.fail(node, std::format("track {} is there twice", track.track));
    }
    font.tracks.push_back(std::move(track));
  }
  return reader.finish(std::move(font), Context);
}

const FieldMusicOverride* MusicTable::overrideOf(std::int64_t mode) const {
  const auto found = std::ranges::find(fieldOverrides, mode, &FieldMusicOverride::mode);
  return found == fieldOverrides.end() ? nullptr : &*found;
}

core::Result<MusicTable> readMusicTable(std::string_view yaml) {
  constexpr std::string_view TableContext = "data/music";
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", TableContext, tree.error().message)}};
  }
  YamlReader reader;
  MusicTable table;
  if (const YamlNode* tracks = tree->find("bgm_tracks"); tracks != nullptr) {
    for (const YamlNode& node : reader.sequence(*tracks, "bgm_tracks")) {
      BgmTrack entry{.id = reader.text(reader.field(node, "id")), .font = {}, .track = {}};
      const auto index = reader.integer<std::int64_t>(reader.field(node, "index"));
      if (reader.ok() && index != static_cast<std::int64_t>(table.bgmTracks.size())) {
        reader.fail(node, "bgm_tracks are in the order of their index");
      }
      if (const YamlNode* font = node.find("font"); font != nullptr) {
        entry.font = reader.integer<std::int64_t>(*font);
        entry.track = reader.integer<std::int64_t>(reader.field(node, "track"));
      }
      table.bgmTracks.push_back(std::move(entry));
    }
  }
  for (const YamlNode& value :
       reader.sequence(reader.field(*tree, "enemy_music_mapping"), "enemy_music_mapping")) {
    table.enemyMusicMapping.push_back(reader.integer<std::int64_t>(value));
  }
  if (const YamlNode* overrides = tree->find("field_overrides"); overrides != nullptr) {
    for (const YamlNode& node : reader.sequence(*overrides, "field_overrides")) {
      table.fieldOverrides.push_back(
          {.mode = reader.integer<std::int64_t>(reader.field(node, "mode")),
           .font = reader.integer<std::int64_t>(reader.field(node, "font")),
           .track = reader.integer<std::int64_t>(reader.field(node, "track"))});
    }
  }
  if (const YamlNode* battle = tree->find("battle"); battle != nullptr) {
    const auto at = [&](std::string_view key) {
      return reader.integer<std::int64_t>(reader.field(*battle, key));
    };
    table.battle = BattleMusic{.font = at("font"),
                               .boss = reader.text(reader.field(*battle, "boss")),
                               .bossTrack = at("boss_track"),
                               .scriptedTrack = at("scripted_track"),
                               .defaultTrack = at("default_track"),
                               .firstEnemyType = at("first_enemy_type"),
                               .endEnemyType = at("end_enemy_type")};
  }
  return reader.finish(std::move(table), TableContext);
}

} // namespace dwe::assets
