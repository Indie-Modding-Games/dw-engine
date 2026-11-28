// The world's clock while the co-op host is held (docs/architecture/coop-training.md T7, T9).
#include <dwe/game/field/field.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/scene/world.hpp>
#include <dwe/session/game_session.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include "coop_gameplay_bridge.hpp"
#include "coop_training.hpp"
#include "packages/game/coop/tests/support/coop_pair.hpp"
#include "packages/game/field/tests/fixture.hpp"
#include "session_fixture.hpp"

using namespace dwe::app;
namespace coop = dwe::game::coop;
using dwe::game::field::Field;
using dwe::game::field::testing::originalRules;
using dwe::game::field::testing::roomMap;
using dwe::net::Millis;
using dwe::session::ClockShare;
using dwe::session::GameSession;

namespace {

constexpr double TickSeconds = 1.0 / 60.0;
constexpr Millis TickMillis{16};
constexpr std::int64_t FramesPerMinute = 20;
constexpr int MaxTicks = 600;

// Ticked as the game loop does: the field, then the session.
struct Player {
  std::unique_ptr<GameSession> session;
  dwe::scene::World world;
  Field field{world, originalRules()};
  dwe::assets::MapDocument map = roomMap("room");

  Player(const dwe::session::testing::TestPacks& packs, dwe::game::save::SaveStorage& storage,
         ClockShare share)
      : session{dwe::session::testing::newGame(packs, &storage)} {
    session->shareClock(share);
    REQUIRE(field.enterMap(map, 0).has_value());
    session->attachField(field);
  }
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;
  Player(Player&&) = delete;
  Player& operator=(Player&&) = delete;
  ~Player() { session->detachField(); }

  void tick() {
    field.tick(static_cast<float>(TickSeconds));
    session->tick(TickSeconds);
  }

  void settle() {
    for (int step = 0; step < MaxTicks && !field.gameTimeRan(); ++step) {
      tick();
    }
    REQUIRE(field.gameTimeRan());
  }

  [[nodiscard]] std::int64_t frames() const { return session->clock().elapsedFrames(); }
  [[nodiscard]] std::int32_t food() const { return session->partner().state().hunger.foodLevel; }
  [[nodiscard]] std::int32_t hours() const { return session->partner().state().evolution.hours; }
};

struct Pair {
  dwe::session::testing::TestPacks packs;
  dwe::game::save::MemorySaveStorage storage;
  coop::test::CoopPair link;
  NetworkFieldController onHost;
  NetworkFieldController unused;
  CoopGameplayBridge hostSide{link.host(), onHost};
  CoopGameplayBridge guestSide{link.guest(), unused};
  Player host{packs, storage, ClockShare::Host};
  Player guest{packs, storage, ClockShare::Guest};
  CoopTraining hostTraining;
  CoopTraining guestTraining;
  // A host screen (a shop): the loop ticks only GameSession::tickHeldPlayer.
  bool hostScreen = false;

  Pair() {
    REQUIRE(link.connect());
    host.settle();
    guest.settle();
    run(0.5);
  }

  void tick() {
    if (hostScreen) {
      host.session->tickHeldPlayer(TickSeconds);
    } else {
      host.tick();
    }
    const std::vector<CoopTrainingPeer> peers{{.id = coop::GuestPlayerId, .bridge = &hostSide}};
    hostTraining.hostTick(*host.session, {}, peers, TickSeconds);
    guest.tick();
    guestTraining.guestTick(*guest.session, {}, coop::GuestPlayerId, guestSide);
    link.network().advance(TickMillis);
    hostSide.poll(link.now());
    guestSide.poll(link.now());
  }

  void run(double seconds) {
    for (double passed = 0.0; passed < seconds; passed += TickSeconds) {
      tick();
    }
  }
};

void run(Player& player, double seconds) {
  for (double passed = 0.0; passed < seconds; passed += TickSeconds) {
    player.tick();
  }
}

} // namespace

TEST_CASE("coop host clock: a script of the host's own runs the world's clock on for the guest; "
          "the host's partner stands, then lives that time once let go (T7)") {
  Pair pair;
  const std::int64_t start = pair.host.frames();
  const std::int32_t food = pair.host.food();
  pair.host.field.holdForScript(true);
  pair.run(30.0);
  // Thirty real seconds of a talk are thirty minutes of the world, for both games.
  const std::int64_t ran = pair.host.frames() - start;
  CHECK(ran >= (30 * FramesPerMinute) - 3);
  CHECK(ran <= (30 * FramesPerMinute) + 3);
  CHECK(std::abs(pair.guest.frames() - pair.host.frames()) <= FramesPerMinute);
  CHECK(pair.host.session->worldTime().running);
  CHECK(pair.host.food() == food);
  pair.host.field.holdForScript(false);
  pair.run(0.1);
  CHECK(pair.host.food() < food);
}

TEST_CASE("coop host clock: the host's partner lives what it stood as it would have played it") {
  dwe::session::testing::TestPacks packs;
  dwe::game::save::MemorySaveStorage storage;
  Player held{packs, storage, ClockShare::Host};
  Player free{packs, storage, ClockShare::Host};
  held.settle();
  free.settle();
  REQUIRE(held.frames() == free.frames());
  const std::int32_t hours = held.hours();
  // An hour and a half of a talk: one whole hour is skipped as a script's time skip does, the
  // rest is lived frame by frame.
  held.field.holdForScript(true);
  run(held, 90.0);
  run(free, 90.0);
  CHECK(held.frames() == free.frames());
  CHECK(held.hours() == hours);
  CHECK(free.hours() > hours);
  held.field.holdForScript(false);
  held.tick();
  free.tick();
  CHECK(held.hours() == free.hours());
  CHECK(std::abs(held.food() - free.food()) <= 1);
}

TEST_CASE("coop host clock: alone a script stands the clock, as the original; a guest's own talk "
          "stands its clock and catches up after, as before") {
  dwe::session::testing::TestPacks packs;
  dwe::game::save::MemorySaveStorage storage;
  Player alone{packs, storage, ClockShare::Own};
  alone.settle();
  alone.field.holdForScript(true);
  run(alone, 0.2);
  const std::int64_t stood = alone.frames();
  run(alone, 10.0);
  CHECK(alone.frames() == stood);
  alone.session->tickHeldPlayer(TickSeconds);
  CHECK(alone.frames() == stood);

  Pair pair;
  pair.guest.field.holdForScript(true);
  pair.run(0.2);
  const std::int64_t guestStood = pair.guest.frames();
  pair.run(10.0);
  CHECK(pair.guest.frames() == guestStood);
  CHECK(pair.guest.session->clock().elapsedFrames() < pair.host.frames());
  pair.guest.session->tickHeldPlayer(TickSeconds);
  CHECK(pair.guest.frames() == guestStood);
  pair.guest.field.holdForScript(false);
  pair.run(0.5);
  CHECK(std::abs(pair.guest.frames() - pair.host.frames()) <= FramesPerMinute);
}

TEST_CASE("coop host clock: a screen of the host's own (a shop) no longer stands the world's "
          "clock (T9)") {
  Pair pair;
  const std::int64_t start = pair.host.frames();
  const std::int32_t food = pair.host.food();
  pair.hostScreen = true;
  pair.run(20.0);
  const std::int64_t ran = pair.host.frames() - start;
  CHECK(ran >= (20 * FramesPerMinute) - 3);
  CHECK(std::abs(pair.guest.frames() - pair.host.frames()) <= FramesPerMinute);
  CHECK(pair.host.food() == food);
  pair.hostScreen = false;
  pair.run(0.1);
  CHECK(pair.host.food() < food);
}

TEST_CASE("coop host clock: the gym's menu is lived before the course; the course's hour is not "
          "lived twice") {
  dwe::session::testing::TestPacks packs;
  dwe::game::save::MemorySaveStorage storage;
  Player host{packs, storage, ClockShare::Host};
  host.settle();
  // The gym's menu: a script of the host's own.
  host.field.holdForScript(true);
  run(host, 20.0);
  const std::int32_t food = host.food();
  // The course starts under the same script: the menu's minutes are lived at once.
  host.session->holdForCourse(true);
  host.tick();
  const std::int32_t afterMenu = host.food();
  CHECK(afterMenu < food);
  run(host, 30.0);
  CHECK(host.food() == afterMenu);
  host.session->holdForCourse(false);
  const std::int32_t afterCourse = host.food();
  // Only the script's own two minutes after the course, not the course's half hour again.
  run(host, 2.0);
  host.field.holdForScript(false);
  host.tick();
  CHECK(host.food() < afterCourse);
  CHECK(afterCourse - host.food() <= 3);
}
