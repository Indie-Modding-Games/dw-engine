#include <dwe/game/save/storage.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/shared_clock.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>

#include "session_fixture.hpp"

// docs/architecture/coop-training.md: one clock for the whole co-op world, the host's; a training
// session costs its partner an hour and never moves it.
using dwe::game::save::MemorySaveStorage;
using dwe::session::ClockFollow;
using dwe::session::ClockShare;
using dwe::session::GameSession;
using dwe::session::WorldClockFollower;
using dwe::session::WorldClockTeller;
using dwe::session::WorldTime;
using dwe::session::testing::newGame;
using dwe::session::testing::TestPacks;
using dwe::session::testing::Tick;

namespace {

constexpr double FramesPerSecond = 20.0;
constexpr std::int64_t FramesPerMinute = 20;
constexpr std::int64_t FramesPerHour = FramesPerMinute * 60;
// 8:00 of the first day, a new game's start.
constexpr std::int64_t Start = 8 * FramesPerHour;

dwe::game::partner::TrainingRequest hpCourse() {
  return {.place = "trn", .course = "hp"};
}

void run(GameSession& session, double seconds) {
  for (double passed = 0.0; passed < seconds; passed += Tick) {
    session.tick(Tick);
  }
}

} // namespace

TEST_CASE("coop clock: a guest follows the world's clock, plays a small gap, skips whole hours") {
  WorldClockFollower follower;
  CHECK_FALSE(follower.worldFrames(FramesPerSecond));
  CHECK(follower.follow(Start, FramesPerSecond, FramesPerMinute) == ClockFollow{});
  follower.hear({.elapsedFrames = Start + 30, .running = true, .speed = 1.0});
  CHECK(follower.follow(Start, FramesPerSecond, FramesPerMinute) ==
        ClockFollow{.frames = 30, .skippedHours = 0, .backTo = std::nullopt});
  // The host slept: three whole hours and a bit.
  follower.hear({.elapsedFrames = Start + (3 * FramesPerHour) + 7, .running = true, .speed = 1.0});
  CHECK(follower.follow(Start, FramesPerSecond, FramesPerMinute) ==
        ClockFollow{.frames = 7, .skippedHours = 3, .backTo = std::nullopt});
}

TEST_CASE("coop clock: the world runs on between words at the host's speed, at most two seconds, "
          "and stands while the host's clock stands") {
  WorldClockFollower follower;
  follower.hear({.elapsedFrames = Start, .running = true, .speed = 1.0});
  follower.pass(0.5);
  CHECK(follower.worldFrames(FramesPerSecond) == Start + 10);
  follower.pass(10.0);
  CHECK(follower.worldFrames(FramesPerSecond) == Start + 40);
  follower.hear({.elapsedFrames = Start + 40, .running = false, .speed = 1.0});
  follower.pass(5.0);
  CHECK(follower.worldFrames(FramesPerSecond) == Start + 40);
  follower.hear({.elapsedFrames = Start, .running = true, .speed = 60.0});
  follower.pass(1.0);
  CHECK(follower.worldFrames(FramesPerSecond) == Start + FramesPerHour);
}

TEST_CASE("coop clock: a guest a little ahead waits; far ahead (its own time skip) goes back") {
  WorldClockFollower follower;
  follower.hear({.elapsedFrames = Start, .running = false, .speed = 1.0});
  CHECK(follower.follow(Start + 30, FramesPerSecond, FramesPerMinute) == ClockFollow{});
  CHECK(follower.follow(Start + FramesPerHour, FramesPerSecond, FramesPerMinute) ==
        ClockFollow{.frames = 0, .skippedHours = 0, .backTo = Start});
}

TEST_CASE("coop clock: the host tells its clock at once, then about once a second, and at once "
          "when it stops or jumps") {
  WorldClockTeller teller;
  WorldTime now{.elapsedFrames = Start, .running = true, .speed = 1.0};
  CHECK(teller.due(now, Tick, FramesPerSecond));
  now.elapsedFrames += 1;
  CHECK_FALSE(teller.due(now, 0.05, FramesPerSecond));
  now.running = false;
  CHECK(teller.due(now, Tick, FramesPerSecond));
  CHECK_FALSE(teller.due(now, 0.5, FramesPerSecond));
  CHECK(teller.due(now, 0.5, FramesPerSecond));
  now.elapsedFrames += FramesPerHour;
  CHECK(teller.due(now, Tick, FramesPerSecond));
  teller.tellAgain();
  CHECK(teller.due(now, Tick, FramesPerSecond));
}

TEST_CASE("coop clock: alone, a session moves the clock an hour, as the original") {
  TestPacks packs;
  MemorySaveStorage storage;
  const auto session = newGame(packs, &storage);
  const std::int64_t before = session->clock().elapsedFrames();
  REQUIRE(session->train(hpCourse()).has_value());
  CHECK(session->clock().elapsedFrames() == before + FramesPerHour);
}

TEST_CASE(
    "coop clock: in co-op a session costs its partner the hour and leaves the world's clock") {
  TestPacks packs;
  MemorySaveStorage storage;
  for (const ClockShare share : {ClockShare::Host, ClockShare::Guest}) {
    CAPTURE(static_cast<int>(share));
    const auto session = newGame(packs, &storage);
    session->shareClock(share);
    const std::int64_t before = session->clock().elapsedFrames();
    const std::int32_t hours = session->partner().state().evolution.hours;
    REQUIRE(session->train(hpCourse()).has_value());
    CHECK(session->clock().elapsedFrames() == before);
    CHECK(session->partner().state().evolution.hours == hours + 1);
  }
}

TEST_CASE("coop clock: four players training at once never move the world's clock more than its "
          "own time") {
  TestPacks packs;
  MemorySaveStorage storage;
  const auto host = newGame(packs, &storage);
  host->shareClock(ClockShare::Host);
  std::array<std::unique_ptr<GameSession>, 3> guests{
      newGame(packs, &storage), newGame(packs, &storage), newGame(packs, &storage)};
  for (auto& guest : guests) {
    guest->shareClock(ClockShare::Guest);
  }
  const std::int64_t before = host->clock().elapsedFrames();
  // Ten seconds of play; every player trains a session each second.
  for (int second = 0; second < 10; ++second) {
    REQUIRE(host->train(hpCourse()).has_value());
    for (auto& guest : guests) {
      REQUIRE(guest->train(hpCourse()).has_value());
    }
    for (double passed = 0.0; passed < 1.0; passed += Tick) {
      host->tick(Tick);
      for (auto& guest : guests) {
        guest->hearWorldClock(host->worldTime());
        guest->tick(Tick);
      }
    }
  }
  // Ten real seconds are ten game minutes, whatever the forty sessions.
  const std::int64_t ran = host->clock().elapsedFrames() - before;
  CHECK(ran >= 10 * FramesPerMinute - 2);
  CHECK(ran <= 10 * FramesPerMinute + 2);
  for (auto& guest : guests) {
    CHECK(std::abs(guest->clock().elapsedFrames() - host->clock().elapsedFrames()) <= 2);
    CHECK(guest->partner().state().evolution.hours == host->partner().state().evolution.hours);
  }
}

TEST_CASE("coop clock: in co-op the course's partner waits for its hour while the world runs on") {
  TestPacks packs;
  MemorySaveStorage storage;
  const auto host = newGame(packs, &storage);
  host->shareClock(ClockShare::Host);
  const auto alone = newGame(packs, &storage);
  host->holdForCourse(true);
  const std::int64_t before = host->clock().elapsedFrames();
  const std::int32_t food = host->partner().state().hunger.foodLevel;
  run(*host, 30.0);
  run(*alone, 30.0);
  // The world's clock ran the course's thirty seconds; the partner lived none of them.
  CHECK(host->clock().elapsedFrames() >= before + (29 * FramesPerMinute));
  CHECK(host->partner().state().hunger.foodLevel == food);
  CHECK(alone->partner().state().hunger.foodLevel < food);
  host->holdForCourse(false);
  run(*host, 5.0);
  CHECK(host->partner().state().hunger.foodLevel < food);
}

TEST_CASE("coop clock: a guest's own skip goes back to the world's time and is not lived twice") {
  TestPacks packs;
  MemorySaveStorage storage;
  const auto guest = newGame(packs, &storage);
  guest->shareClock(ClockShare::Guest);
  const std::int64_t world = guest->clock().elapsedFrames();
  guest->hearWorldClock({.elapsedFrames = world, .running = true, .speed = 1.0});
  // Its own skip (a sleep, a solo battle's time) puts its clock two hours ahead; its partner paid
  // for them already.
  static_cast<void>(guest->clock().jumpTo(10, 0));
  guest->partner().skipHours(2, guest->clock());
  const std::int32_t hours = guest->partner().state().evolution.hours;
  guest->tick(Tick);
  CHECK(guest->clock().elapsedFrames() <= world + 1);
  // The world plays on through those two hours: the partner does not live them again.
  for (int minute = 0; minute <= 120; minute += 2) {
    guest->hearWorldClock(
        {.elapsedFrames = world + (minute * FramesPerMinute), .running = true, .speed = 1.0});
    run(*guest, 0.1);
  }
  CHECK(guest->partner().state().evolution.hours == hours);
  // Past them it lives the world's hours again.
  guest->hearWorldClock({.elapsedFrames = world + (2 * FramesPerHour) + FramesPerHour,
                         .running = true,
                         .speed = 1.0});
  run(*guest, 0.1);
  CHECK(guest->partner().state().evolution.hours == hours + 1);
}

TEST_CASE("coop clock: the host slept: a guest skips those hours, as a script's time skip does") {
  TestPacks packs;
  MemorySaveStorage storage;
  const auto guest = newGame(packs, &storage);
  guest->shareClock(ClockShare::Guest);
  const std::int64_t world = guest->clock().elapsedFrames();
  const std::int32_t hours = guest->partner().state().evolution.hours;
  guest->hearWorldClock(
      {.elapsedFrames = world + (8 * FramesPerHour), .running = true, .speed = 1.0});
  guest->tick(Tick);
  CHECK(guest->clock().time().hour == 16);
  CHECK(guest->partner().state().evolution.hours == hours + 8);
}

TEST_CASE("coop clock: a guest's sleep waits for the world's morning; it neither goes back to the "
          "night nor lives the night twice (#424)") {
  TestPacks packs;
  MemorySaveStorage storage;
  const auto guest = newGame(packs, &storage);
  const auto& sleep = guest->partner().state().sleep;
  static_cast<void>(guest->clock().jumpTo(sleep.sleepyHour, sleep.sleepyMinute));
  run(*guest, 0.2);
  REQUIRE(guest->partner().canSleep());
  guest->shareClock(ClockShare::Guest);
  const std::int64_t night = guest->clock().elapsedFrames();
  guest->hearWorldClock({.elapsedFrames = night, .running = true, .speed = 1.0});
  CHECK_FALSE(guest->aheadOfWorld());

  REQUIRE(guest->fallAsleep({}));
  const std::int64_t morning = guest->clock().elapsedFrames();
  REQUIRE(morning > night + FramesPerHour);
  CHECK(guest->aheadOfWorld());
  const std::int32_t hours = guest->partner().state().evolution.hours;
  const std::int32_t age = guest->partner().state().life.age;

  // The world's clock reaches the partner's morning while it sleeps.
  for (std::int64_t world = night; world < morning - FramesPerMinute; world += FramesPerHour) {
    guest->hearWorldClock({.elapsedFrames = world, .running = true, .speed = 1.0});
    CHECK(guest->aheadOfWorld());
  }
  guest->hearWorldClock(
      {.elapsedFrames = morning - FramesPerMinute, .running = true, .speed = 1.0});
  CHECK_FALSE(guest->aheadOfWorld());
  guest->wakeUp();
  run(*guest, 0.5);
  CHECK(guest->clock().elapsedFrames() >= morning);
  CHECK(guest->partner().state().evolution.hours == hours);
  CHECK(guest->partner().state().life.age == age);
}

TEST_CASE("coop clock: a guest asleep ahead of the world knows how long it still waits (#424)") {
  WorldClockFollower follower;
  CHECK_FALSE(follower.waitSeconds(Start, FramesPerSecond, FramesPerMinute));
  follower.hear({.elapsedFrames = Start, .running = true, .speed = 1.0});
  const std::int64_t waits = WorldClockFollower::BackAfterMinutes * FramesPerMinute;
  CHECK(follower.waitSeconds(Start, FramesPerSecond, FramesPerMinute) == 0.0);
  CHECK(follower.waitSeconds(Start + waits + 200, FramesPerSecond, FramesPerMinute) == 10.0);
  follower.hear({.elapsedFrames = Start, .running = true, .speed = 2.0});
  CHECK(follower.waitSeconds(Start + waits + 200, FramesPerSecond, FramesPerMinute) == 5.0);
  follower.hear({.elapsedFrames = Start, .running = false, .speed = 1.0});
  CHECK_FALSE(follower.waitSeconds(Start + waits + 200, FramesPerSecond, FramesPerMinute));
}
