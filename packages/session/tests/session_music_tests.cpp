#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/audio/output.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/session_music.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "packages/engine/assets/tests/media_fixtures.hpp"

using namespace dwe;

namespace {

// A font with a day song (0) and a night song (1).
constexpr std::string_view TwoSongs = R"(tracks:
- {track: 0, file: "track_00.mid", plays: forever}
- {track: 1, file: "track_01.mid", plays: forever}
)";

void addFont(assets::MemoryFileSystem& memory, int font) {
  const std::string folder = std::format("pack/music/{:03}/", font);
  memory.addFile(folder + "font_meta.yaml", TwoSongs);
  memory.addFile(folder + "font.sf2", assets::testing::soundFontFile());
  memory.addFile(folder + "track_00.mid", assets::testing::midiFile());
  memory.addFile(folder + "track_01.mid", assets::testing::midiFile());
}

assets::VirtualFileSystem mountedFonts(assets::MemoryFileSystem& memory) {
  memory.addFile("pack/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  addFont(memory, 1);
  addFont(memory, 28);
  const std::vector<std::filesystem::path> roots{"pack"};
  return assets::VirtualFileSystem::mount(memory, roots).value();
}

// How many songs have started (sound.c: seqPlay) and how many stops (sound.c: stopBGM) the log
// has had.
struct MusicLog {
  std::size_t starts = 0;
  std::size_t stops = 0;
};

MusicLog& musicLog() {
  static MusicLog log;
  static const bool added = [] {
    core::addLogSink([](core::LogLevel /*level*/, std::string_view message) {
      log.starts += message.contains("seqPlay (sound.c:488)") ? 1 : 0;
      log.stops += message.contains("stopBGM (sound.c:779)") ? 1 : 0;
    });
    return true;
  }();
  static_cast<void>(added);
  return log;
}

struct MusicRig {
  assets::MemoryFileSystem memory;
  assets::VirtualFileSystem files{mountedFonts(memory)};
  audio::Audio audio{std::make_unique<audio::NullOutput>()};
  std::int32_t hour = 12;
  session::SessionMusic music{
      audio, files,
      session::MusicFacts{.mode = [] { return std::int64_t{0}; }, .hour = [this] { return hour; }}};

  // map.c: changeMap: the map's head section, with its set_bgm when it has one.
  void enterMap(std::optional<std::int64_t> font) {
    music.enterMap();
    if (font) {
      music.playBgm(*font);
    }
    music.mapHeadDone();
  }
};

} // namespace

TEST_CASE("every map plays its song from the start, as the original") {
  MusicRig rig;
  const MusicLog before = musicLog();
  rig.enterMap(28);
  rig.enterMap(28);
  rig.enterMap(28);
  CHECK(musicLog().starts - before.starts == 3);
}

TEST_CASE("continuous music lets the song of the maps that share it go on and changes it once") {
  MusicRig rig;
  rig.music.setContinuousMusic(true);
  const MusicLog before = musicLog();
  rig.enterMap(28);
  rig.enterMap(28);
  rig.enterMap(28);
  CHECK(musicLog().starts - before.starts == 1);
  CHECK(musicLog().stops - before.stops == 1);
  // Another font: cut and the new song from its start, then it goes on across its maps.
  rig.enterMap(1);
  rig.enterMap(1);
  CHECK(musicLog().starts - before.starts == 2);
  REQUIRE(rig.music.activeSong().has_value());
  CHECK(rig.music.activeSong()->font == 1);
}

TEST_CASE("continuous music changes the song when the hour picks the night one") {
  MusicRig rig;
  rig.music.setContinuousMusic(true);
  const MusicLog before = musicLog();
  rig.enterMap(28);
  rig.hour = 22;
  rig.enterMap(28);
  CHECK(musicLog().starts - before.starts == 2);
  REQUIRE(rig.music.activeSong().has_value());
  CHECK(rig.music.activeSong()->track == 1);
}

TEST_CASE("a map whose head picks no song is silent") {
  for (const bool continuous : {false, true}) {
    MusicRig rig;
    rig.music.setContinuousMusic(continuous);
    rig.enterMap(28);
    const MusicLog before = musicLog();
    rig.enterMap(std::nullopt);
    CHECK(musicLog().stops - before.stops == 1);
    CHECK_FALSE(rig.music.activeSong().has_value());
    // The next map's song starts again.
    rig.enterMap(28);
    CHECK(musicLog().starts - before.starts == 1);
  }
}

TEST_CASE("a script's set_bgm outside a map change plays the song from its start") {
  MusicRig rig;
  rig.music.setContinuousMusic(true);
  rig.enterMap(28);
  const MusicLog before = musicLog();
  rig.music.playBgm(28);
  CHECK(musicLog().starts - before.starts == 1);
}
