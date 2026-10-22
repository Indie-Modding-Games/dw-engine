#include <dwe/assets/map_document.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/save/autosave.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/scene/world.hpp>
#include <dwe/session/game_session.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <ostream>
#include <vector>

#include "fixture.hpp"
#include "session_fixture.hpp"

using dwe::assets::MapDocument;
using dwe::game::field::Field;
using dwe::game::field::MapChange;
using dwe::game::field::testing::fill;
using dwe::game::field::testing::originalRules;
using dwe::game::field::testing::roomMap;
using dwe::game::save::AutosaveReason;
using dwe::game::save::MemorySaveStorage;
using dwe::session::Autosave;
using dwe::session::GameSession;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGame;
using dwe::session::testing::TestPacks;
using dwe::session::testing::Tick;

namespace {

constexpr std::int32_t TownToHouseExit = 2;
constexpr std::uint8_t TownToHouseTrigger = 112;
constexpr std::int32_t MaxTicks = 600;
// Rounding of the clock's frame fractions over many ticks.
constexpr double FrameSlack = 1e-6;

// A town whose exit 2 leads to a house, as in the field's warp tests.
struct Maps {
  MapDocument town = roomMap("town");
  MapDocument house = roomMap("house");

  Maps() {
    fill(town, {.column = 50, .row = 40}, {.column = 52, .row = 41}, TownToHouseTrigger);
    town.warps[TownToHouseExit] = {.exit = TownToHouseExit,
                                   .spawn = {0, 0, 300},
                                   .rotation = 0,
                                   .targetMap = -1,
                                   .targetMapId = "house",
                                   .targetExit = 0};
  }
};

// What a run of the game loop saw: the ticks without game time and the autosaves.
struct LoopRun {
  std::int32_t ticks = 0;
  std::int32_t stoppedTicks = 0;
  std::vector<Autosave> autosaves;
};

// The game loop: the field ticks, the app enters the map it asks for, then the session ticks.
class GameLoop {
public:
  GameLoop(GameSession& session, const Maps& maps)
      : session_(session), maps_(maps), field_(world_, originalRules()) {
    REQUIRE(field_.enterMap(maps_.town, TownToHouseExit).has_value());
    session_.attachField(field_);
  }
  GameLoop(const GameLoop&) = delete;
  GameLoop& operator=(const GameLoop&) = delete;
  GameLoop(GameLoop&&) = delete;
  GameLoop& operator=(GameLoop&&) = delete;
  ~GameLoop() { session_.detachField(); }

  [[nodiscard]] Field& field() { return field_; }

  LoopRun run(std::int32_t ticks) {
    LoopRun seen;
    for (std::int32_t step = 0; step < ticks; ++step) {
      field_.tick(static_cast<float>(Tick));
      if (const std::optional<MapChange> change = field_.takeMapChange()) {
        REQUIRE(field_.enterMap(maps_.house, change->exit).has_value());
      }
      session_.tick(Tick);
      ++seen.ticks;
      if (!field_.gameTimeRan()) {
        ++seen.stoppedTicks;
      }
      if (const std::optional<Autosave> autosave = session_.takeAutosaved()) {
        seen.autosaves.push_back(*autosave);
      }
    }
    return seen;
  }

  // Walks into the house and on until the fade in ends.
  LoopRun walkIntoHouse() {
    field_.setInput({.stick = {0.0F, 1.0F}});
    LoopRun walked;
    for (std::int32_t step = 0; step < MaxTicks && field_.mapId() != "house"; ++step) {
      append(walked, run(1));
    }
    REQUIRE(field_.mapId() == "house");
    field_.setInput({});
    append(walked, run(MaxTicks / 2));
    return walked;
  }

private:
  static void append(LoopRun& total, const LoopRun& more) {
    total.ticks += more.ticks;
    total.stoppedTicks += more.stoppedTicks;
    total.autosaves.insert(total.autosaves.end(), more.autosaves.begin(), more.autosaves.end());
  }

  GameSession& session_;
  const Maps& maps_;
  dwe::scene::World world_;
  Field field_;
};

std::int64_t minutesOf(const dwe::ruleset::GameTime& time) {
  return (time.hour * dwe::ruleset::MinutesPerHour) + time.minute;
}

} // namespace

TEST_CASE(
    "on the field the game clock keeps the old field clock's pace: a game minute per real second") {
  const TestPacks packs;
  const auto session = newGame(packs, nullptr);
  const Maps maps;
  GameLoop loop(*session, maps);
  // The first map fades in with game time stopped, as every map does.
  const std::int64_t before = minutesOf(session->clock().time());
  for (std::int32_t step = 0; step < MaxTicks && !loop.field().gameTimeRan(); ++step) {
    static_cast<void>(loop.run(1));
  }
  REQUIRE(loop.field().gameTimeRan());
  CHECK(minutesOf(session->clock().time()) == before);

  const std::int64_t start = minutesOf(session->clock().time());
  constexpr std::int32_t Seconds = 90;
  const LoopRun ran = loop.run(static_cast<std::int32_t>(std::lround(Seconds / Tick)));
  CHECK(ran.stoppedTicks == 0);
  CHECK(minutesOf(session->clock().time()) == start + Seconds);
}

TEST_CASE("game time stands still through a warp's fades, at any clock speed") {
  const TestPacks packs;
  const Maps maps;
  std::vector<std::int32_t> stoppedAt;
  for (const double speed : {1.0, 60.0}) {
    CAPTURE(speed);
    const auto session = newGame(packs, nullptr);
    session->clock().setSpeed(speed);
    GameLoop loop(*session, maps);
    const std::int64_t before = session->clock().elapsedFrames();
    const LoopRun walked = loop.walkIntoHouse();
    CHECK(walked.stoppedTicks > 0);
    // The clock ran only on the ticks the field played.
    const double played = (walked.ticks - walked.stoppedTicks) * Tick * speed;
    const double frames = played * session->clock().rules().framesPerSecond;
    CHECK(session->clock().elapsedFrames() - before ==
          static_cast<std::int64_t>(std::floor(frames + FrameSlack)));
    stoppedAt.push_back(walked.stoppedTicks);
  }
  // The fades take the same frames whatever the clock's speed.
  REQUIRE(stoppedAt.size() == 2);
  CHECK(stoppedAt[0] == stoppedAt[1]);
}

TEST_CASE("a map change autosaves, and the save keeps where the player is") {
  const TestPacks packs;
  MemorySaveStorage storage;
  const auto session = newGame(packs, &storage);
  const Maps maps;
  {
    GameLoop loop(*session, maps);
    // Entering the first map is no map change.
    CHECK(loop.run(1).autosaves.empty());
    const LoopRun walked = loop.walkIntoHouse();
    REQUIRE(walked.autosaves.size() == 1);
    CHECK(walked.autosaves.front().reasons == std::vector{AutosaveReason::MapChange});
  }
  const std::optional<dwe::session::FieldPlace> place = session->savedPlace();
  if (!place) {
    FAIL("no saved place");
    return;
  }
  CHECK(place->map == "house");

  const auto saved = session->saveFile();
  REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
  CHECK(saved->header.label == "agum house");
  const auto loaded = newGame(packs, nullptr, 3);
  REQUIRE(loaded->loadFile(*saved).has_value());
  const std::optional<dwe::session::FieldPlace> loadedPlace = loaded->savedPlace();
  if (!loadedPlace) {
    FAIL("no loaded place");
    return;
  }
  CHECK(loadedPlace->map == "house");
  CHECK(loadedPlace->spawn.position == place->spawn.position);
}

TEST_CASE(
    "how the partner feels about the map follows the map entered, for the care rules and the HUD") {
  using dwe::game::partner::AreaResponse;
  const TestPacks packs;
  MemorySaveStorage storage;
  const auto session = newGame(packs, &storage);
  const std::int32_t favored = session->partner().species().favoredRegion;
  constexpr std::int32_t NoRegion = dwe::assets::MapRegions::NoRegion;
  Maps maps;
  maps.town.regions = {.a = {favored, NoRegion, NoRegion, NoRegion},
                       .b = {NoRegion, NoRegion, NoRegion, NoRegion}};
  maps.house.regions = {.a = {favored + 1, NoRegion, NoRegion, NoRegion},
                        .b = {NoRegion, favored, NoRegion, NoRegion}};
  CHECK(session->areaResponse() == AreaResponse::Neutral);
  {
    GameLoop loop(*session, maps);
    // The first map counts too, though entering it is no map change.
    CHECK(session->areaResponse() == AreaResponse::Liked);
    CHECK(session->careContext().area == AreaResponse::Liked);
    CHECK(session->partnerView().area == AreaResponse::Liked);
    loop.walkIntoHouse();
    CHECK(session->areaResponse() == AreaResponse::Disliked);
    CHECK(session->careContext().area == AreaResponse::Disliked);
    CHECK(session->partnerView().area == AreaResponse::Disliked);
  }
  CHECK(session->areaResponse() == AreaResponse::Neutral);
}
