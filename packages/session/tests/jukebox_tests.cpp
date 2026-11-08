#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/jukebox.hpp>
#include <dwe/session/story_adapter.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"

using dwe::game::evolution::testing::TableFixture;
using dwe::session::GameSession;
using dwe::session::Jukebox;
using dwe::session::JukeboxDesc;
using dwe::session::JukeboxListView;
using dwe::session::JukeboxMusic;
using dwe::session::MusicSong;
using dwe::session::SessionSystem;
using dwe::session::StoryAdapter;
using dwe::session::StoryAdapterDesc;
using dwe::session::testing::evolutionAdapter;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

constexpr std::int32_t UnlockTrigger = 50;
constexpr std::int32_t ChosenVariable = 249;
constexpr std::uint8_t NoTrack = 255;
constexpr std::int32_t Visible = 6;
// The music menu's entries: BGM_TRACK_NAMES has 63, the last one locked.
constexpr std::int32_t Entries = 63;

// A made-up font and song per entry, not the original's: three songs per font.
MusicSong madeUpSong(std::int32_t entry) {
  return {.font = 1 + (entry / 3), .track = entry % 3};
}

// data/music.yaml with the entries' fonts and songs, or without them.
std::string musicTable(bool withSongs) {
  std::string yaml = "bgm_tracks:\n";
  for (std::int32_t entry = 0; entry < Entries; ++entry) {
    yaml +=
        std::format("- id: \"song_{}\"\n  index: {}\n  name: \"Song {}\"\n", entry, entry, entry);
    if (withSongs) {
      const MusicSong song = madeUpSong(entry);
      yaml += std::format("  font: {}\n  track: {}\n", song.font, song.track);
    }
  }
  return yaml + "enemy_music_mapping: []\n";
}

// The music, as calls.
class FakeMusic final : public JukeboxMusic {
public:
  [[nodiscard]] std::optional<MusicSong> activeSong() const override { return active; }
  void playSong(MusicSong song) override { played.push_back(song); }
  void stopSong() override { ++stops; }
  void replayActive() override { ++replays; }

  std::optional<MusicSong> active;
  std::vector<MusicSong> played;
  int stops = 0;
  int replays = 0;
};

struct Visit {
  std::unique_ptr<GameSession> session;
  StoryAdapter* story = nullptr;
  Jukebox* jukebox = nullptr;

  [[nodiscard]] std::int32_t chosen() const {
    return story->state().variable(ChosenVariable).value_or(0);
  }
  void unlock() const { REQUIRE(story->state().setTrigger(UnlockTrigger, true)); }
};

Visit sessionWithJukebox(const TestPacks& packs) {
  const TableFixture forms;
  auto evolution = evolutionAdapter(packs, forms);
  auto story = std::make_unique<StoryAdapter>(StoryAdapterDesc{.files = &packs.files()});
  auto jukebox =
      std::make_unique<Jukebox>(JukeboxDesc{.files = &packs.files(), .story = story.get()});
  Visit visit{.session = nullptr, .story = story.get(), .jukebox = jukebox.get()};
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(evolution));
  systems.push_back(std::move(story));
  systems.push_back(std::move(jukebox));
  visit.session = newGameWith(packs, std::move(systems));
  return visit;
}

} // namespace

TEST_CASE("the jukebox reads the original ruleset: MAPHEAD 0x4d6, trigger 50 locks song 62") {
  const TestPacks packs{{"data/music.yaml", musicTable(true)}};
  const Visit visit = sessionWithJukebox(packs);
  REQUIRE(visit.jukebox->available());
  const auto& rules = visit.jukebox->rules();
  CHECK(rules.linesSection == 0x4d6);
  CHECK(rules.lines.greeting == 3);
  CHECK(rules.lines.ask == 0);
  CHECK(rules.lines.chosen == 2);
  CHECK(rules.lines.farewell == 1);
  CHECK(rules.unlockTrigger == UnlockTrigger);
  CHECK(rules.lockedTracks == std::vector<std::int32_t>{62});
  CHECK(rules.chosenVariable == ChosenVariable);
  CHECK(rules.noTrack == NoTrack);
}

TEST_CASE("jukeboxFillTitleList lists 62 songs, and 63 once trigger 50 is set") {
  const TestPacks packs{{"data/music.yaml", musicTable(true)}};
  const Visit visit = sessionWithJukebox(packs);
  FakeMusic music;
  REQUIRE(visit.jukebox->open(music));
  CHECK(visit.jukebox->rows().size() == 62);
  CHECK(visit.jukebox->rows().back() == 61);
  visit.jukebox->close();
  visit.unlock();
  REQUIRE(visit.jukebox->open(music));
  CHECK(visit.jukebox->rows().size() == 63);
  CHECK(visit.jukebox->rows().back() == 62);
}

TEST_CASE("the map's song is marked in variable 249 and the list opens on its row") {
  const TestPacks packs{{"data/music.yaml", musicTable(true)}};
  const Visit visit = sessionWithJukebox(packs);
  FakeMusic music;
  SUBCASE("in the first window") {
    music.active = madeUpSong(4);
    REQUIRE(visit.jukebox->open(music));
    CHECK(visit.chosen() == 4);
    CHECK(visit.jukebox->chosenTrack() == 4);
    const JukeboxListView view = visit.jukebox->startView(Visible);
    CHECK(view.top == 0);
    CHECK(view.cursor == 4);
  }
  SUBCASE("past it: at the window's bottom") {
    music.active = madeUpSong(10);
    REQUIRE(visit.jukebox->open(music));
    const JukeboxListView view = visit.jukebox->startView(Visible);
    CHECK(view.top == 5);
    CHECK(view.cursor == 5);
    CHECK(view.index() == 10);
  }
  CHECK(music.stops == 0);
  CHECK(music.played.empty());
}

TEST_CASE("a map song the list lacks stops the music and marks none") {
  const TestPacks packs{{"data/music.yaml", musicTable(true)}};
  const Visit visit = sessionWithJukebox(packs);
  FakeMusic music;
  music.active = MusicSong{.font = 30, .track = 2};
  REQUIRE(visit.jukebox->open(music));
  CHECK(music.stops == 1);
  CHECK(visit.chosen() == NoTrack);
  CHECK_FALSE(visit.jukebox->chosenTrack());
  const JukeboxListView view = visit.jukebox->startView(Visible);
  CHECK(view.index() == 0);
  // tickJukeboxMenu: none chosen and none played, nothing starts.
  CHECK_FALSE(visit.jukebox->playChosen());
}

TEST_CASE("a pick plays its song once over the map's, and leaving plays the map's again") {
  const TestPacks packs{{"data/music.yaml", musicTable(true)}};
  const Visit visit = sessionWithJukebox(packs);
  FakeMusic music;
  music.active = madeUpSong(2);
  REQUIRE(visit.jukebox->open(music));
  // The song that already plays is not started again.
  REQUIRE(visit.jukebox->choose(2));
  CHECK_FALSE(visit.jukebox->playChosen());
  REQUIRE(visit.jukebox->choose(7));
  CHECK(visit.chosen() == 7);
  CHECK(visit.jukebox->playChosen());
  CHECK_FALSE(visit.jukebox->playChosen());
  REQUIRE(music.played.size() == 1);
  CHECK(music.played.front() == madeUpSong(7));
  CHECK(music.stops == 1);
  CHECK_FALSE(visit.jukebox->choose(62));
  visit.jukebox->close();
  CHECK(music.replays == 1);
  CHECK_FALSE(visit.jukebox->isOpen());
}

TEST_CASE("a pack without the entries' fonts finds them in the fonts' font_meta") {
  const TestPacks packs{
      {"data/music.yaml", musicTable(false)},
      {"music/009/font_meta.yaml",
       "tracks:\n- track: 0\n  file: \"track_00.mid\"\n  plays: forever\n  used_by:\n"
       "    jukebox:\n    - \"song_5\"\n- track: 1\n  file: \"track_01.mid\"\n  plays: once\n"
       "  used_by:\n    jukebox: []\n"}};
  const Visit visit = sessionWithJukebox(packs);
  FakeMusic music;
  REQUIRE(visit.jukebox->open(music));
  REQUIRE(visit.jukebox->choose(5));
  CHECK(visit.jukebox->playChosen());
  REQUIRE(music.played.size() == 1);
  CHECK(music.played.front() == MusicSong{.font = 9, .track = 0});
  // An entry no font names plays nothing.
  REQUIRE(visit.jukebox->choose(6));
  CHECK_FALSE(visit.jukebox->playChosen());
}

TEST_CASE("the list's cursor stops at the ends and R1 jumps to them") {
  constexpr std::int32_t Count = 62;
  JukeboxListView view;
  CHECK_FALSE(view.up());
  for (std::int32_t step = 0; step < Visible; ++step) {
    CHECK(view.down(Count, Visible));
  }
  CHECK(view.top == 1);
  CHECK(view.cursor == Visible - 1);
  view.toBottom(Count, Visible);
  CHECK(view.top == Count - Visible);
  CHECK(view.cursor == Visible - 1);
  CHECK_FALSE(view.down(Count, Visible));
  view.toTop();
  CHECK(view.index() == 0);
  JukeboxListView few;
  few.toBottom(3, Visible);
  CHECK(few.top == 0);
  CHECK(few.cursor == 2);
}
