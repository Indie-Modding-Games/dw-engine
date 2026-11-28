// The shared world's sync diagnostics (#359): updates lost and late on their way, map changes and
// divergences between the host's authority and what each guest holds.
#include <dwe/game/coop/sync_watch.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <string>
#include <variant>
#include <vector>

#include "support/coop_room.hpp"

using namespace dwe::game::coop;
using dwe::net::Millis;

namespace {

SyncHeld place(PlayerId id, std::string map, std::int32_t x) {
  return {.id = id, .mapId = std::move(map), .x = x, .y = 0, .z = 0};
}

SyncReport reportOf(std::uint32_t sequence, std::vector<SyncHeld> players, std::uint32_t lost = 0,
                    std::uint32_t late = 0) {
  return {.sequence = sequence,
          .snapshotTick = 10,
          .snapshotsLost = lost,
          .snapshotsLate = late,
          .players = std::move(players)};
}

bool mentions(const std::vector<std::string>& events, std::string_view word) {
  return std::ranges::any_of(events,
                             [&](const std::string& event) { return event.contains(word); });
}

// A room of `guests` + 1 over real links: the host asks every guest to report, each reports what
// it holds twice a second for 2.5 s; the last guest holds the host 500 units off and the first one
// says it lost snapshots. The host's monitor must tell them apart.
void runRoom(std::size_t guests) {
  CAPTURE(guests);
  test::CoopRoom room{{.guests = guests}};
  REQUIRE(room.connect());
  REQUIRE(room.sendAll(SyncWatch{.on = true}));
  room.pump();
  for (auto& seat : room.seats()) {
    const auto got = test::CoopPair::drain(*seat.guest);
    CHECK(std::ranges::any_of(got, [](const Message& message) {
      return std::holds_alternative<SyncWatch>(message) && std::get<SyncWatch>(message).on;
    }));
  }
  std::vector<SyncHeld> truth{place(0, "room", 0)};
  for (const auto& seat : room.seats()) {
    truth.push_back(place(seat.id, "room", 100 * seat.id));
  }
  const auto off = static_cast<PlayerId>(guests);
  SyncMonitor monitor{{.tolerance = 100.0F, .persistAfter = Millis{1000}}};
  std::vector<std::string> events;
  for (std::uint32_t round = 1; round <= 5; ++round) {
    const Millis now{static_cast<std::int64_t>(round) * 500};
    monitor.authority(truth, round * 10, now);
    for (auto& seat : room.seats()) {
      std::vector<SyncHeld> held = truth;
      if (seat.id == off) {
        held.front().x = 500;
      }
      REQUIRE(seat.guest->send(reportOf(round, std::move(held), seat.id == 1 ? round * 2 : 0)));
    }
    room.pump();
    for (auto& seat : room.seats()) {
      for (const Message& message : test::CoopPair::drain(*seat.host)) {
        if (const auto* report = std::get_if<SyncReport>(&message)) {
          monitor.report(seat.id, *report, now);
        }
      }
    }
    for (std::string& event : monitor.takeEvents()) {
      events.push_back(std::move(event));
    }
  }
  const auto divergences = monitor.divergences();
  REQUIRE(divergences.size() == 1);
  CHECK(divergences.front().guest == off);
  CHECK(divergences.front().player == HostPlayerId);
  CHECK(divergences.front().persistent);
  CHECK(mentions(events, std::format("diverged: player {} holds player 0", off)));
  CHECK(mentions(events, "lost: player 1 missed 2 more snapshots"));
  CHECK(mentions(events, "2500 ms, tick 50: lost: player 1 missed 2 more snapshots (10 in all)"));
  const auto lines = monitor.lines(Millis{2500});
  for (const auto& seat : room.seats()) {
    CHECK(mentions(lines, std::format("guest {}: report 0 ms ago", seat.id)));
  }
}

} // namespace

TEST_CASE("two players over a real link: the host's diagnostics see the guest's report") {
  runRoom(1);
}

TEST_CASE("three players over real links: the host's diagnostics tell each guest apart") {
  runRoom(2);
}

TEST_CASE("four players over real links: the host's diagnostics tell each guest apart") {
  runRoom(3);
}

TEST_CASE("a sequence watch counts the gaps as lost and the stragglers as late, not lost") {
  SequenceWatch watch;
  CHECK(watch.observe(10) == SequenceWatch::Arrival::First);
  CHECK(watch.observe(11) == SequenceWatch::Arrival::InOrder);
  CHECK(watch.observe(14) == SequenceWatch::Arrival::AfterGap);
  CHECK(watch.lost() == 2);
  CHECK(watch.observe(12) == SequenceWatch::Arrival::Late);
  CHECK(watch.lost() == 1);
  CHECK(watch.late() == 1);
  CHECK(watch.observe(14) == SequenceWatch::Arrival::Repeat);
  CHECK(watch.repeats() == 1);
  CHECK(*watch.newest() == 14);
  // Past the rollover it keeps counting in order.
  SequenceWatch wrapping;
  (void)wrapping.observe(0xFFFFFFFFU);
  CHECK(wrapping.observe(0) == SequenceWatch::Arrival::InOrder);
  CHECK(wrapping.lost() == 0);
}

TEST_CASE("the monitor finds a guest holding a player elsewhere and calls it persistent in time") {
  SyncMonitor monitor{{.tolerance = 100.0F, .persistAfter = Millis{1000}}};
  monitor.authority({place(0, "room", 0), place(1, "room", 500)}, 30, Millis{0});
  monitor.report(1, reportOf(1, {place(0, "room", 40), place(1, "room", 500)}), Millis{0});
  CHECK(monitor.divergences().empty());
  CHECK(monitor.converged(Millis{0}));

  // The guest keeps the host 900 units off: off at once, persistent after a second.
  monitor.report(1, reportOf(2, {place(0, "room", 900), place(1, "room", 500)}), Millis{100});
  REQUIRE(monitor.divergences().size() == 1);
  CHECK_FALSE(monitor.divergences().front().persistent);
  CHECK_FALSE(monitor.converged(Millis{0}));
  monitor.report(1, reportOf(3, {place(0, "room", 900), place(1, "room", 500)}), Millis{1200});
  REQUIRE(monitor.divergences().size() == 1);
  CHECK(monitor.divergences().front().persistent);
  CHECK(monitor.divergences().front().distance == doctest::Approx(900.0F));
  CHECK(mentions(monitor.takeEvents(), "diverged: player 1 holds player 0"));

  // Back in place: the divergence ends, and says so.
  monitor.report(1, reportOf(4, {place(0, "room", 0), place(1, "room", 500)}), Millis{1300});
  CHECK(monitor.divergences().empty());
  CHECK(mentions(monitor.takeEvents(), "in sync"));
  // A report that comes late changes nothing.
  monitor.report(1, reportOf(3, {place(0, "room", 900)}), Millis{1400});
  CHECK(monitor.divergences().empty());
}

TEST_CASE("the monitor logs map changes and the updates lost or late both ways") {
  SyncMonitor monitor;
  monitor.authority({place(0, "room", 0), place(1, "room", 0)}, 1, Millis{0});
  monitor.authority({place(0, "hall", 0), place(1, "hall", 0)}, 2, Millis{50});
  const auto maps = monitor.takeEvents();
  CHECK(mentions(maps, "map: player 0 went from room to hall"));
  CHECK(mentions(maps, "map: player 1 went from room to hall"));

  // The guest still on the old map: a divergence of map, not of distance.
  monitor.report(1, reportOf(1, {place(0, "room", 0), place(1, "hall", 0)}, 3, 1), Millis{60});
  REQUIRE(monitor.divergences().size() == 1);
  CHECK(monitor.divergences().front().distance < 0.0F);
  CHECK(monitor.divergences().front().authorityMap == "hall");
  const auto counts = monitor.takeEvents();
  CHECK(mentions(counts, "lost: player 1 missed 3 more snapshots"));
  CHECK(mentions(counts, "late: player 1 got 1 more snapshots"));

  SequenceWatch states;
  (void)states.observe(1);
  (void)states.observe(5);
  monitor.states(1, states);
  CHECK(mentions(monitor.takeEvents(), "the host missed 3 more states of player 1"));
  const auto lines = monitor.lines(Millis{100});
  CHECK(lines.size() > 3);
  CHECK(mentions(lines, "guest 1:"));

  monitor.forget(1);
  CHECK(monitor.divergences().empty());
}
