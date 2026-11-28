// How a viewer draws another player's states (#358): late and lost states, and the teleports that
// must be drawn as jumps.
#include <dwe/game/coop/authority.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

using namespace dwe::game::coop;
namespace net = dwe::net;

namespace {

net::Millis ms(std::int64_t value) {
  return net::Millis{value};
}

PlayerPose runAt(std::int32_t x, std::int32_t z = 0, std::string map = "mayo01") {
  return {.mapId = std::move(map), .x = x, .y = 0, .z = z, .yaw = 0, .motion = Motion::Run};
}

// States every 50 ms of a run along x at 1000 units a second, arriving 30 ms after they were sent.
void runTo(PoseTrack& track, std::int64_t last) {
  for (std::int64_t i = 0; i <= last; ++i) {
    REQUIRE(track.push(static_cast<std::uint32_t>(i), ms(i * 50), runAt(static_cast<int>(i * 50)),
                       ms((i * 50) + 30)));
  }
}

} // namespace

TEST_CASE("a pose track draws a short teleport as a jump, not a slide") {
  PoseTrack track;
  runTo(track, 10);
  // A script puts the player 300 units back between two states 50 ms apart: no run gets there.
  REQUIRE(track.push(11, ms(550), runAt(200, 0), ms(580)));
  // Up to the teleport's time it is drawn where it was; from then on where it went.
  const auto before = track.sample(ms(580 + 100 - 5));
  REQUIRE(before);
  CHECK(before->x == doctest::Approx(500.0F));
  const auto after = track.sample(ms(580 + 100 + 1));
  REQUIRE(after);
  CHECK(after->x == doctest::Approx(200.0F));
  // A sideways one too, and no run on from it toward where it came from.
  REQUIRE(track.push(12, ms(600), runAt(200, 250), ms(630)));
  const auto aside = track.sample(ms(630 + 100 + 60));
  REQUIRE(aside);
  CHECK(aside->x == doctest::Approx(200.0F));
  CHECK(aside->z == doctest::Approx(250.0F));
}

TEST_CASE("a pose track slides a long run across lost states and catches up without a jump") {
  PoseTrack track;
  runTo(track, 10);
  // States 11 to 21 are lost (600 ms) while the player runs on: a long way, but no teleport. The
  // viewer draws a frame every 16 ms as the states come.
  std::int64_t next = 22;
  auto last = track.sample(ms(500));
  REQUIRE(last);
  float largest = 0.0F;
  for (std::int64_t now = 516; now <= 1600; now += 16) {
    for (; next <= 30 && (next * 50) + 30 <= now; ++next) {
      REQUIRE(track.push(static_cast<std::uint32_t>(next), ms(next * 50),
                         runAt(static_cast<int>(next * 50)), ms((next * 50) + 30)));
    }
    const auto pose = track.sample(ms(now));
    REQUIRE(pose);
    largest = std::max(largest, std::abs(pose->x - last->x));
    last = pose;
  }
  // It held near where the states stopped, then caught up within the delay: never a jump.
  CHECK(largest < 100.0F);
  const auto caught = track.sample(ms(1150 + 30 + 100));
  REQUIRE(caught);
  CHECK(caught->x == doctest::Approx(1150.0F));
}

TEST_CASE("a pose track takes a late state still ahead of its playback, and drops one played") {
  PoseTrack track;
  // A state every 3 ticks (50 ms); the one of tick 12 is held up on the way.
  for (std::int64_t i = 0; i <= 5; ++i) {
    if (i != 4) {
      REQUIRE(track.push(static_cast<std::uint32_t>(i * 3), ms(i * 50),
                         runAt(0, static_cast<int>(i * 10)), ms((i * 50) + 30)));
    }
  }
  // It shows a turn the others do not, and arrives after the next one but before the playback
  // reached its time.
  CHECK(track.push(12, ms(200), runAt(0, 140), ms(290)));
  const auto filled = track.sample(ms(200 + 30 + 100));
  REQUIRE(filled);
  CHECK(filled->z == doctest::Approx(140.0F));
  CHECK(track.newest() == 15U);
  // One whose time was already played, and one held already, change nothing.
  CHECK_FALSE(track.push(4, ms(66), runAt(0, 900), ms(400)));
  CHECK_FALSE(track.push(12, ms(200), runAt(0, 900), ms(300)));
  CHECK(track.sample(ms(330))->z == doctest::Approx(140.0F));
}

TEST_CASE("a pose track never goes back to a map for a straggler sent before the change") {
  PoseTrack track;
  REQUIRE(track.push(1, ms(50), runAt(10), ms(80)));
  REQUIRE(track.push(3, ms(150), runAt(0, 0, "mayo02"), ms(180)));
  CHECK_FALSE(track.push(2, ms(100), runAt(20), ms(190)));
  CHECK(track.mapId() == "mayo02");
  CHECK(track.newest() == 3U);
}
