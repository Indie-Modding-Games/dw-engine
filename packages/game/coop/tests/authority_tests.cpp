// The host's authority over the co-op session (#353), the per-player transform it keeps (#354) and
// the player registry (#355).
#include <dwe/game/coop/authority.hpp>
#include <dwe/game/coop/link.hpp>
#include <dwe/net/loopback.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace dwe::game::coop;
namespace net = dwe::net;

namespace {

PlayerState stateAt(PlayerId player, std::uint32_t sequence, std::int32_t x,
                    std::uint16_t epoch = 1, std::string map = "mayo01") {
  return {.player = player,
          .sequence = sequence,
          .epoch = epoch,
          .mapId = std::move(map),
          .x = x,
          .y = 0,
          .z = 0,
          .yaw = 0,
          .motion = Motion::Run};
}

PlayerPose poseAt(std::int32_t x, std::string map = "mayo01") {
  return {.mapId = std::move(map), .x = x, .y = 0, .z = 0, .yaw = 0, .motion = Motion::Idle};
}

net::Millis ms(std::int64_t value) {
  return net::Millis{value};
}

// A session with the host on mayo01 and one guest put at x = 0 (epoch 1).
WorldAuthority sessionWithGuest() {
  WorldAuthority authority{"Host", "agumon"};
  authority.setWorldMap("mayo01");
  REQUIRE(authority.join("Guest", "gabumon", ms(0)) == GuestPlayerId);
  REQUIRE(authority.relocate(GuestPlayerId, poseAt(0)));
  return authority;
}

} // namespace

TEST_CASE("the registry gives each joining guest the lowest free id, up to four players") {
  WorldAuthority authority{"Host", "agumon"};
  CHECK(authority.size() == 1);
  REQUIRE(authority.find(HostPlayerId) != nullptr);
  CHECK(authority.find(HostPlayerId)->partnerSpecies == "agumon");

  CHECK(authority.join("A", "gabumon", ms(0)) == PlayerId{1});
  CHECK(authority.join("B", "", ms(0)) == PlayerId{2});
  CHECK(authority.join("C", "", ms(0)) == PlayerId{3});
  CHECK(authority.full());
  // A fifth player is refused and changes nothing.
  const std::uint32_t revision = authority.revision();
  CHECK_FALSE(authority.join("D", "", ms(0)));
  CHECK(authority.revision() == revision);
  CHECK(authority.size() == MaxPlayers);

  // Leaving frees the id; the next joiner takes the lowest free one, never a duplicate.
  CHECK(authority.leave(2));
  CHECK_FALSE(authority.leave(2));
  CHECK_FALSE(authority.leave(HostPlayerId));
  CHECK(authority.find(2) == nullptr);
  CHECK(authority.join("E", "", ms(0)) == PlayerId{2});
  const Roster roster = authority.roster();
  REQUIRE(roster.players.size() == 4);
  for (std::size_t i = 0; i < roster.players.size(); ++i) {
    CHECK(roster.players[i].id == i);
  }
  CHECK(roster.players[2].name == "E");
  // The roster survives the wire.
  const auto decoded = decode(encode(Message{roster}));
  REQUIRE(decoded);
  REQUIRE(decoded->has_value());
  CHECK(std::get<Roster>(**decoded) == roster);
}

TEST_CASE("a guest that reconnects is a clean new record") {
  WorldAuthority authority = sessionWithGuest();
  REQUIRE(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 10, 50), ms(100)) ==
          StateVerdict::Accepted);
  REQUIRE(authority.leave(GuestPlayerId));
  // Its old link's late state finds nobody.
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 11, 60), ms(120)) ==
        StateVerdict::UnknownPlayer);
  REQUIRE(authority.join("Guest", "gabumon", ms(200)) == GuestPlayerId);
  const PlayerRecord* back = authority.find(GuestPlayerId);
  REQUIRE(back != nullptr);
  CHECK(back->epoch == 0);
  CHECK_FALSE(back->sequence);
  CHECK_FALSE(back->pose);
  // Until the host puts it somewhere (epoch 1), the old link's epoch-1 states do not land.
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 12, 60), ms(220)) ==
        StateVerdict::OldEpoch);
  const auto spawn = authority.relocate(GuestPlayerId, poseAt(400));
  REQUIRE(spawn);
  CHECK(spawn->epoch == 1);
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 1, 420), ms(300)) ==
        StateVerdict::Accepted);
}

TEST_CASE("the authority keeps the newest valid state of each player and drops stale ones") {
  WorldAuthority authority = sessionWithGuest();
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 30, 300), ms(500)) ==
        StateVerdict::Accepted);
  // Older or repeated sequence numbers never overwrite a newer state.
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 27, 270), ms(510)) ==
        StateVerdict::Stale);
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 30, 999), ms(520)) ==
        StateVerdict::Stale);
  REQUIRE(authority.find(GuestPlayerId)->pose);
  CHECK(authority.find(GuestPlayerId)->pose->x == 300);
  CHECK(authority.find(GuestPlayerId)->sequence == 30U);
  // Sequence numbers roll over.
  WorldAuthority rolling = sessionWithGuest();
  CHECK(rolling.accept(GuestPlayerId, stateAt(GuestPlayerId, 0xFFFFFFF0U, 10), ms(100)) ==
        StateVerdict::Accepted);
  CHECK(rolling.accept(GuestPlayerId, stateAt(GuestPlayerId, 4, 20), ms(200)) ==
        StateVerdict::Accepted);
  CHECK(rolling.accept(GuestPlayerId, stateAt(GuestPlayerId, 0xFFFFFFF8U, 15), ms(210)) ==
        StateVerdict::Stale);
}

TEST_CASE("a client cannot write authoritative state") {
  WorldAuthority authority = sessionWithGuest();
  REQUIRE(authority.join("Second", "", ms(0)) == PlayerId{2});
  REQUIRE(authority.relocate(2, poseAt(1000)));
  // Guest 1 cannot move guest 2, nor the host.
  CHECK(authority.accept(GuestPlayerId, stateAt(2, 5, 1000), ms(50)) == StateVerdict::NotOwner);
  CHECK(authority.accept(HostPlayerId, stateAt(2, 5, 1000), ms(50)) == StateVerdict::UnknownPlayer);
  // Not on the session's map.
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 5, 0, 1, "mayo02"), ms(50)) ==
        StateVerdict::WrongMap);
  // Farther than a player can move: 6 ticks (0.1 s) for 5000 units.
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 6, 5000), ms(100)) ==
        StateVerdict::TooFar);
  CHECK(authority.find(GuestPlayerId)->pose->x == 0);
  // Skipping sequence numbers buys no distance: the real time caps it.
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 60'000, 50'000), ms(150)) ==
        StateVerdict::TooFar);
  // Refused again and again, the host corrects it back to its newest valid pose with a new epoch,
  // and the states it sent before applying it no longer count.
  const auto correction = authority.correctionDue(GuestPlayerId);
  REQUIRE(correction);
  CHECK(correction->epoch == 2);
  CHECK(correction->x == 0);
  CHECK(correction->mapId == "mayo01");
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 70, 20, 1), ms(200)) ==
        StateVerdict::OldEpoch);
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 71, 20, 2), ms(220)) ==
        StateVerdict::Accepted);
  // The protocol layer refuses the same writes before they reach the authority: a guest sending
  // the host's messages loses its link (link.cpp: directionAllowed).
  net::LoopbackNetwork network({});
  const Identity identity{.protocol = ProtocolVersion, .gameVersion = "1", .contentHash = "h"};
  CoopLink host = CoopLink::host(network.link(0), identity);
  CoopLink guest = CoopLink::guest(network.link(1), identity, "Guest");
  for (int i = 0; i < 8; ++i) {
    network.advance(ms(1));
    host.poll(network.now());
    guest.poll(network.now());
  }
  REQUIRE(guest.phase() == LinkPhase::Ready);
  CHECK_FALSE(guest.send(
      Message{Roster{.revision = 1, .players = {{.id = 0, .name = "x", .partnerSpecies = {}}}}}));
  CHECK_FALSE(guest.send(Message{PlayerCorrection{
      .player = 1, .epoch = 1, .mapId = "m", .exit = {}, .x = 0, .y = 0, .z = 0, .yaw = 0}}));
  CHECK_FALSE(host.send(Message{stateAt(GuestPlayerId, 1, 0)}));
  CHECK(guest.send(Message{stateAt(GuestPlayerId, 1, 0)}));
}

TEST_CASE("the welcome gives the guest the id its registry chose") {
  net::LoopbackNetwork network({});
  const Identity identity{.protocol = ProtocolVersion, .gameVersion = "1", .contentHash = "h"};
  CoopLink host = CoopLink::host(network.link(0), identity, "Host", {}, PlayerId{3});
  CoopLink guest = CoopLink::guest(network.link(1), identity, "Guest");
  for (int i = 0; i < 8; ++i) {
    network.advance(ms(1));
    host.poll(network.now());
    guest.poll(network.now());
  }
  REQUIRE(guest.phase() == LinkPhase::Ready);
  CHECK(host.remotePlayer() == PlayerId{3});
  CHECK(guest.localPlayer() == PlayerId{3});
  CHECK(host.localPlayer() == HostPlayerId);
  guest.leave(LeaveReason::Quit);
  network.advance(ms(1));
  host.poll(network.now());
  CHECK(host.phase() == LinkPhase::Left);
  CHECK(host.leaveReason() == LeaveReason::Quit);
}

TEST_CASE("player states over a jittery, lossy, duplicating link keep the newest one") {
  // 60 ms plus up to 80 ms of jitter: datagrams reorder; 10% are lost and 10% arrive twice.
  net::LoopbackNetwork network(
      {.latency = ms(60), .jitter = ms(80), .lossPercent = 10, .duplicatePercent = 10}, 7);
  const Identity identity{.protocol = ProtocolVersion, .gameVersion = "1", .contentHash = "h"};
  CoopLink host = CoopLink::host(network.link(0), identity);
  CoopLink guest = CoopLink::guest(network.link(1), identity, "Guest");
  for (int i = 0; i < 400 && guest.phase() != LinkPhase::Ready; ++i) {
    network.advance(ms(5));
    host.poll(network.now());
    guest.poll(network.now());
  }
  REQUIRE(guest.phase() == LinkPhase::Ready);
  WorldAuthority authority = sessionWithGuest();
  std::uint32_t accepted = 0;
  std::uint32_t refusedStale = 0;
  std::uint32_t newest = 0;
  // The guest runs right at 900 units a second and sends its state every 3 ticks (20 Hz).
  for (std::uint32_t tick = 3; tick <= 600; tick += 3) {
    const auto x = static_cast<std::int32_t>(std::lround(900.0 * tick / 60.0));
    REQUIRE(guest.send(Message{stateAt(GuestPlayerId, tick, x)}));
    for (int step = 0; step < 10; ++step) {
      network.advance(ms(5));
      guest.poll(network.now());
      host.poll(network.now());
      while (auto message = host.receive()) {
        const auto* state = std::get_if<PlayerState>(&*message);
        REQUIRE(state != nullptr);
        const StateVerdict verdict = authority.accept(GuestPlayerId, *state, network.now());
        if (verdict == StateVerdict::Accepted) {
          ++accepted;
          CHECK(sequenceNewer(state->sequence, newest));
          newest = state->sequence;
        } else {
          CHECK(verdict == StateVerdict::Stale);
          ++refusedStale;
        }
      }
    }
  }
  for (int step = 0; step < 40; ++step) {
    network.advance(ms(5));
    host.poll(network.now());
    while (auto message = host.receive()) {
      const auto& state = std::get<PlayerState>(*message);
      if (authority.accept(GuestPlayerId, state, network.now()) == StateVerdict::Accepted) {
        newest = state.sequence;
      }
    }
  }
  // Reordering and duplicates happened and were dropped; whatever arrived last, the record holds
  // the newest state that made it, and its position is that state's.
  CHECK(refusedStale > 0);
  CHECK(accepted > 100);
  const PlayerRecord* record = authority.find(GuestPlayerId);
  REQUIRE(record->sequence);
  CHECK(*record->sequence == newest);
  CHECK(record->pose->x == static_cast<std::int32_t>(std::lround(900.0 * newest / 60.0)));
}

TEST_CASE("four players keep one transform each, by id") {
  WorldAuthority authority{"Host", "agumon"};
  authority.setWorldMap("mayo01");
  for (PlayerId id = 1; id < MaxPlayers; ++id) {
    REQUIRE(authority.join("P", "", ms(0)) == id);
    REQUIRE(authority.relocate(id, poseAt(id * 1000)));
  }
  authority.setHostPose(poseAt(-500), ms(0));
  for (std::uint32_t tick = 1; tick <= 30; ++tick) {
    for (PlayerId id = 1; id < MaxPlayers; ++id) {
      const auto x = static_cast<std::int32_t>((id * 1000) + (tick * 10));
      CHECK(authority.accept(id, stateAt(id, tick, x), ms(tick * 16)) == StateVerdict::Accepted);
    }
  }
  for (PlayerId id = 1; id < MaxPlayers; ++id) {
    CHECK(authority.find(id)->pose->x == static_cast<std::int32_t>((id * 1000) + 300));
  }
  CHECK(authority.find(HostPlayerId)->pose->x == -500);
  // One leaves; the others are untouched.
  REQUIRE(authority.leave(2));
  CHECK(authority.find(1)->pose->x == 1300);
  CHECK(authority.find(3)->pose->x == 3300);
  CHECK(authority.roster().players.size() == 3);
}

TEST_CASE("a quiet guest keeps its last state and is found silent only past the limit") {
  WorldAuthority authority = sessionWithGuest();
  REQUIRE(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 3, 40), ms(100)) ==
          StateVerdict::Accepted);
  // Standing still for a long time changes nothing of where it is.
  CHECK(authority.silent(ms(5000), ms(8000)).empty());
  CHECK(authority.find(GuestPlayerId)->pose->x == 40);
  // After a long pause the next state is measured against the real time that passed.
  CHECK(authority.accept(GuestPlayerId, stateAt(GuestPlayerId, 900, 9000), ms(15'100)) ==
        StateVerdict::Accepted);
  CHECK(authority.silent(ms(30'000), ms(8000)) == std::vector<PlayerId>{GuestPlayerId});
}

TEST_CASE("a pose track plays remote motion smoothly behind the newest state") {
  PoseTrack track;
  const auto at = [](std::int32_t x, std::string map = "mayo01") {
    return PlayerPose{
        .mapId = std::move(map), .x = x, .y = 0, .z = 0, .yaw = 0, .motion = Motion::Run};
  };
  CHECK_FALSE(track.sample(ms(0)));
  // States every 50 ms, moving 50 units each, arriving with 30 ms of latency.
  for (std::int64_t i = 0; i <= 10; ++i) {
    CHECK(track.push(static_cast<std::uint32_t>(i), ms(i * 50), at(static_cast<int>(i * 50)),
                     ms((i * 50) + 30)));
  }
  // A late or repeated state is dropped.
  CHECK_FALSE(track.push(9, ms(450), at(-999), ms(560)));
  CHECK_FALSE(track.push(10, ms(500), at(-999), ms(560)));
  // 100 ms behind the newest: between states, in proportion to the time.
  const auto middle = track.sample(ms(30 + 400 + 25));
  REQUIRE(middle);
  CHECK(middle->x == doctest::Approx(325.0F));
  // Frame by frame the drawn position only moves forward and by small steps: no snaps.
  float last = -1.0F;
  for (std::int64_t now = 200; now <= 560; now += 16) {
    const auto pose = track.sample(ms(now));
    REQUIRE(pose);
    CHECK(pose->x >= last);
    if (last >= 0.0F) {
      CHECK(pose->x - last <= 20.0F);
    }
    last = pose->x;
  }
  // Starved of states it goes a little past the newest, then holds.
  const auto starved = track.sample(ms(5000));
  REQUIRE(starved);
  CHECK(starved->x == doctest::Approx(600.0F));
  // A correction far away is a jump, not a slide.
  CHECK(track.push(11, ms(550), at(5000), ms(580)));
  const auto before = track.sample(ms(580 + 100 - 20));
  REQUIRE(before);
  CHECK(before->x <= 500.0F + 1.0F);
  const auto after = track.sample(ms(580 + 100 + 1));
  REQUIRE(after);
  CHECK(after->x == doctest::Approx(5000.0F));
  // Another map starts over: the old map's states are gone.
  CHECK(track.push(12, ms(600), at(10, "mayo02"), ms(630)));
  CHECK(track.mapId() == "mayo02");
  const auto moved = track.sample(ms(2000));
  REQUIRE(moved);
  CHECK(moved->x == doctest::Approx(10.0F));
}

TEST_CASE("a pose track picks the motion up smoothly again after the sender's clock paused") {
  PoseTrack track;
  const auto at = [](std::int32_t x) {
    return PlayerPose{.mapId = "mayo01", .x = x, .y = 0, .z = 0, .yaw = 0, .motion = Motion::Run};
  };
  for (std::int64_t i = 0; i <= 10; ++i) {
    REQUIRE(track.push(static_cast<std::uint32_t>(i), ms(i * 50), at(static_cast<int>(i * 50)),
                       ms((i * 50) + 30)));
  }
  // The sender paused for a minute (its clock stood still), then moves on.
  for (std::int64_t i = 11; i <= 20; ++i) {
    REQUIRE(track.push(static_cast<std::uint32_t>(i), ms(i * 50), at(static_cast<int>(i * 50)),
                       ms(60000 + (i * 50) + 30)));
  }
  // Between the states after the pause, not stuck past the newest one.
  const auto pose = track.sample(ms(60000 + 900 + 30 + 25));
  REQUIRE(pose);
  CHECK(pose->x == doctest::Approx(825.0F));
}

TEST_CASE("presence: the players on a map are the ones whose newest valid pose is there") {
  WorldAuthority authority = sessionWithGuest();
  authority.setHostPose(poseAt(0), ms(0));
  REQUIRE(authority.join("Third", "", ms(0)) == PlayerId{2});
  CHECK(authority.playersOn("mayo01") == std::vector<PlayerId>{HostPlayerId, GuestPlayerId});
  // The third has no pose until the host places it.
  REQUIRE(authority.relocate(2, poseAt(0, "mayo02")));
  CHECK(authority.playersOn("mayo02") == std::vector<PlayerId>{2});
  authority.setHostPose(poseAt(0, "mayo02"), ms(10));
  CHECK(authority.playersOn("mayo02") == std::vector<PlayerId>{HostPlayerId, 2});
  CHECK(authority.leave(2));
  CHECK(authority.playersOn("mayo02") == std::vector<PlayerId>{HostPlayerId});
}
