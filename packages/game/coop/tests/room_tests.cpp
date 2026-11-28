#include <doctest/doctest.h>

#include <set>

#include "support/coop_room.hpp"

using namespace dwe::game::coop;
using dwe::game::coop::test::CoopRoom;

namespace {

BattleBegin begunBy(PlayerId starter, std::uint32_t ask) {
  std::vector<PlayerId> allies;
  for (PlayerId player = HostPlayerId; validPlayer(player); ++player) {
    if (player != starter) {
      allies.push_back(player);
    }
  }
  std::vector<std::string> species{"starter", "foe"};
  species.resize(2 + allies.size(), "ally");
  return {.battle = 4,
          .starter = starter,
          .ask = ask,
          .mapId = "forest",
          .enemyNpcs = {3},
          .species = std::move(species),
          .fixedFight = false,
          .allies = std::move(allies),
          .folder = false};
}

} // namespace

TEST_CASE("a room of four links each guest under its own id and hears each on its own link") {
  CoopRoom room;
  REQUIRE(room.connect());
  std::set<PlayerId> ids;
  for (CoopRoom::Seat& seat : room.seats()) {
    CHECK(seat.guest->localPlayer() == seat.id);
    CHECK(seat.host->remotePlayer() == seat.id);
    CHECK(seat.host->remoteName() == "Guest " + std::to_string(seat.id));
    ids.insert(seat.id);
    REQUIRE(seat.guest->send(Message{Ready{.ready = seat.id % 2 == 1}}));
  }
  CHECK(ids == std::set<PlayerId>{1, 2, 3});
  room.pump();
  for (CoopRoom::Seat& seat : room.seats()) {
    const auto heard = dwe::game::coop::test::CoopPair::drain(*seat.host);
    REQUIRE(heard.size() == 1);
    CHECK(std::get<Ready>(heard.front()).ready == (seat.id % 2 == 1));
  }
}

TEST_CASE("a battle that any guest of a room of four starts goes out to every guest (#352)") {
  for (PlayerId starter = HostPlayerId; validPlayer(starter); ++starter) {
    CAPTURE(starter);
    CoopRoom room;
    REQUIRE(room.connect());
    const BattleBegin begin = begunBy(starter, starter == HostPlayerId ? 0 : 7);
    REQUIRE(room.sendAll(Message{begin}));
    room.pump();
    for (CoopRoom::Seat& seat : room.seats()) {
      CAPTURE(seat.id);
      CHECK(seat.guest->phase() == LinkPhase::Ready);
      const auto heard = dwe::game::coop::test::CoopPair::drain(*seat.guest);
      REQUIRE(heard.size() == 1);
      CHECK(std::get<BattleBegin>(heard.front()) == begin);
    }
  }
  // A guest's battle answers its ask; the host's answers none; nobody past the room starts one.
  const auto rejects = [](const BattleBegin& begin) { CHECK_FALSE(decode(encode(begin))); };
  rejects(begunBy(3, 0));
  rejects(begunBy(HostPlayerId, 7));
  BattleBegin past = begunBy(2, 7);
  past.starter = MaxPlayers;
  rejects(past);
}

TEST_CASE("one guest of a room of four dropping leaves the others' links up") {
  CoopRoom room;
  REQUIRE(room.connect());
  room.cut(2);
  room.pump(100, dwe::net::Millis{100});
  CHECK(room.host(2).phase() == LinkPhase::Failed);
  CHECK(room.guest(2).phase() == LinkPhase::Failed);
  for (const PlayerId stays : {PlayerId{1}, PlayerId{3}}) {
    CAPTURE(stays);
    CHECK(room.host(stays).phase() == LinkPhase::Ready);
    CHECK(room.guest(stays).phase() == LinkPhase::Ready);
    REQUIRE(room.host(stays).send(Message{SyncWatch{.on = true}}));
  }
  room.pump();
  CHECK(dwe::game::coop::test::CoopPair::drain(room.guest(1)).size() == 1);
  CHECK(dwe::game::coop::test::CoopPair::drain(room.guest(3)).size() == 1);
}
