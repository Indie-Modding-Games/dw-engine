// Training in a shared world (#365, docs/architecture/coop-training.md): a host and three guests,
// headless, each guest on a loopback link of its own and with a game session of its own. Everyone
// trains at once; the world's clock is the host's, runs only with real time, and every guest's
// follows it; each player sees who trains.
#include <dwe/game/save/storage.hpp>
#include <dwe/net/loopback.hpp>
#include <dwe/session/game_session.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "coop_gameplay_bridge.hpp"
#include "coop_training.hpp"
#include "session_fixture.hpp"

using namespace dwe::app;
namespace coop = dwe::game::coop;
using dwe::net::Millis;
using dwe::session::ClockShare;
using dwe::session::GameSession;

namespace {

constexpr double TickSeconds = 1.0 / 60.0;
constexpr Millis TickMillis{16};
constexpr std::int64_t FramesPerMinute = 20;

coop::Identity identity() {
  return {.protocol = coop::ProtocolVersion, .gameVersion = "test", .contentHash = "content"};
}

dwe::game::partner::TrainingRequest hpCourse() {
  return {.place = "trn", .course = "hp"};
}

struct Guest {
  coop::PlayerId id;
  dwe::net::LoopbackNetwork network;
  coop::CoopLink hostLink;
  coop::CoopLink guestLink;
  NetworkFieldController onHost;
  NetworkFieldController unused;
  CoopGameplayBridge hostSide;
  CoopGameplayBridge guestSide;
  std::unique_ptr<GameSession> session;
  CoopTraining training;
  std::string course;

  Guest(coop::PlayerId player, const dwe::session::testing::TestPacks& packs,
        dwe::game::save::SaveStorage& storage)
      : id{player}, network{{}, 11U + player},
        hostLink{coop::CoopLink::host(network.link(0), identity(), "Host", {}, player)},
        guestLink{coop::CoopLink::guest(network.link(1), identity(), "Guest")},
        hostSide{hostLink, onHost}, guestSide{guestLink, unused},
        session{dwe::session::testing::newGame(packs, &storage)} {
    session->shareClock(ClockShare::Guest);
  }
};

struct Room {
  dwe::session::testing::TestPacks packs;
  dwe::game::save::MemorySaveStorage storage;
  std::unique_ptr<GameSession> host = dwe::session::testing::newGame(packs, &storage);
  CoopTraining training;
  std::string course;
  std::vector<std::unique_ptr<Guest>> guests;
  Millis now{0};

  explicit Room(std::size_t players) {
    host->shareClock(ClockShare::Host);
    for (coop::PlayerId id = 1; id < players; ++id) {
      guests.push_back(std::make_unique<Guest>(id, packs, storage));
    }
    for (int step = 0; step < 100; ++step) {
      poll();
    }
    for (auto& guest : guests) {
      REQUIRE(guest->hostLink.phase() == coop::LinkPhase::Ready);
      REQUIRE(guest->guestLink.localPlayer() == guest->id);
    }
  }

  void poll() {
    now += TickMillis;
    for (auto& guest : guests) {
      guest->network.advance(TickMillis);
      guest->hostSide.poll(now);
      guest->guestSide.poll(now);
    }
  }

  void tick() {
    host->tick(TickSeconds);
    std::vector<CoopTrainingPeer> peers;
    for (auto& guest : guests) {
      peers.push_back({.id = guest->id, .bridge = &guest->hostSide});
    }
    training.hostTick(*host, course, peers, TickSeconds);
    for (auto& guest : guests) {
      guest->session->tick(TickSeconds);
      guest->training.guestTick(*guest->session, guest->course, guest->id, guest->guestSide);
    }
    poll();
  }

  void run(double seconds) {
    for (double passed = 0.0; passed < seconds; passed += TickSeconds) {
      tick();
    }
  }
};

} // namespace

TEST_CASE("coop training: four players train at once, the world's clock runs only with real time "
          "and every guest's follows it") {
  Room room{4};
  const std::int64_t start = room.host->clock().elapsedFrames();
  // Everyone on a course: the host and the three guests, the whole time.
  room.course = "trn_hp";
  for (auto& guest : room.guests) {
    guest->course = "trn_mp";
  }
  for (int second = 0; second < 20; ++second) {
    // Each player's session ends and pays its hour; the next starts at once.
    REQUIRE(room.host->train(hpCourse()).has_value());
    for (auto& guest : room.guests) {
      REQUIRE(guest->session->train(hpCourse()).has_value());
    }
    room.run(1.0);
  }
  // Twenty real seconds are twenty game minutes, however many sessions ran.
  const std::int64_t ran = room.host->clock().elapsedFrames() - start;
  CHECK(ran >= (20 * FramesPerMinute) - 3);
  CHECK(ran <= (20 * FramesPerMinute) + 3);
  for (auto& guest : room.guests) {
    CAPTURE(guest->id);
    CHECK(std::abs(guest->session->clock().elapsedFrames() - room.host->clock().elapsedFrames()) <=
          FramesPerMinute);
    // Each partner paid its twenty hours, as twenty solo sessions would.
    CHECK(guest->session->partner().state().evolution.hours ==
          room.host->partner().state().evolution.hours);
  }
}

TEST_CASE("coop training: every player sees who trains, and a player that leaves stops training") {
  Room room{3};
  room.run(0.5);
  for (coop::PlayerId id = 0; id < 3; ++id) {
    CHECK_FALSE(room.training.training(id));
  }
  room.guests.at(0)->course = "trn_speed";
  room.run(0.5);
  CHECK(room.training.training(1));
  CHECK(room.guests.at(1)->training.training(1));
  CHECK_FALSE(room.guests.at(1)->training.training(0));
  room.course = "trn_hp";
  room.run(0.5);
  CHECK(room.guests.at(0)->training.training(0));
  CHECK(room.guests.at(1)->training.training(0));
  // The trainee's own game says once that the world's time goes on.
  CHECK(room.training.takeClockNotice());
  CHECK_FALSE(room.training.takeClockNotice());
  CHECK(room.guests.at(0)->training.takeClockNotice());
  CHECK_FALSE(room.guests.at(1)->training.takeClockNotice());
  // The course ends; then a guest leaves while on a course.
  room.course.clear();
  room.run(0.5);
  CHECK_FALSE(room.guests.at(1)->training.training(0));
  room.training.playerLeft(1);
  room.guests.erase(room.guests.begin());
  room.run(0.5);
  CHECK_FALSE(room.training.training(1));
  CHECK_FALSE(room.guests.at(0)->training.training(1));
}

TEST_CASE("coop training: the host's jump reaches the guests at once, and a guest that goes on "
          "past the world waits for it") {
  Room room{2};
  room.run(1.0);
  Guest& guest = *room.guests.at(0);
  // The host slept to the next morning.
  static_cast<void>(room.host->clock().jumpTo(7, 0));
  room.run(0.5);
  CHECK(guest.session->clock().time().day == room.host->clock().time().day);
  CHECK(guest.session->clock().time().hour == 7);
  // The host's clock stands (a talk on the host): the guest's waits with it.
  room.host->clock().setRunning(false);
  room.run(0.2);
  const std::int64_t stood = guest.session->clock().elapsedFrames();
  room.run(3.0);
  CHECK(guest.session->clock().elapsedFrames() == stood);
}
