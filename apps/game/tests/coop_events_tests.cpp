#include <dwe/game/coop/link.hpp>
#include <dwe/net/loopback.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <vector>

#include "coop_events.hpp"
#include "coop_gameplay_bridge.hpp"

using namespace dwe::app;
namespace coop = dwe::game::coop;
using dwe::net::Millis;

namespace {

coop::Identity identity() {
  return {.protocol = coop::ProtocolVersion, .gameVersion = "test", .contentHash = "content"};
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
  CoopEvents events;
  std::optional<CoopEvent> own;

  explicit Guest(coop::PlayerId player)
      : id{player}, network{{}, 21U + player},
        hostLink{coop::CoopLink::host(network.link(0), identity(), "Host", {}, player)},
        guestLink{coop::CoopLink::guest(network.link(1), identity(), "Guest")},
        hostSide{hostLink, onHost}, guestSide{guestLink, unused} {}
};

struct Room {
  CoopEvents events;
  std::optional<CoopEvent> own;
  std::vector<std::unique_ptr<Guest>> guests;
  Millis now{0};

  explicit Room(std::size_t players) {
    for (coop::PlayerId id = 1; id < players; ++id) {
      guests.push_back(std::make_unique<Guest>(id));
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
    now += Millis{16};
    for (auto& guest : guests) {
      guest->network.advance(Millis{16});
      guest->hostSide.poll(now);
      guest->guestSide.poll(now);
    }
  }

  void run(int ticks = 20) {
    for (int tick = 0; tick < ticks; ++tick) {
      std::vector<CoopEventPeer> peers;
      for (auto& guest : guests) {
        peers.push_back({.id = guest->id, .bridge = &guest->hostSide});
      }
      events.hostTick(own, peers);
      for (auto& guest : guests) {
        guest->events.guestTick(guest->own, guest->id, guest->guestSide);
      }
      poll();
    }
  }
};

using Held = std::vector<std::int32_t>;

} // namespace

TEST_CASE("coop events: a guest's event holds its NPC in every other game until it ends, so the "
          "host does not start it again (#417)") {
  Room room{3};
  room.run();
  CHECK(room.events.heldByOthers(coop::HostPlayerId, "town").empty());

  room.guests.at(0)->own = CoopEvent{.mapId = "town", .npc = 7};
  room.run();
  CHECK(room.events.heldByOthers(coop::HostPlayerId, "town") == Held{7});
  CHECK(room.guests.at(1)->events.heldByOthers(2, "town") == Held{7});
  CHECK(room.guests.at(0)->events.heldByOthers(1, "town").empty());
  CHECK(room.events.heldByOthers(coop::HostPlayerId, "forest").empty());

  room.own = CoopEvent{.mapId = "town", .npc = 3};
  room.run();
  CHECK(room.guests.at(0)->events.heldByOthers(1, "town") == Held{3});
  CHECK(room.guests.at(1)->events.heldByOthers(2, "town") == Held{3, 7});

  room.guests.at(0)->own.reset();
  room.own.reset();
  room.run();
  CHECK(room.events.heldByOthers(coop::HostPlayerId, "town").empty());
  CHECK(room.guests.at(1)->events.heldByOthers(2, "town").empty());
}

TEST_CASE("coop events: a guest that leaves in the middle of an event lets its NPC go") {
  Room room{3};
  room.guests.at(1)->own = CoopEvent{.mapId = "town", .npc = 5};
  room.run();
  REQUIRE(room.guests.at(0)->events.heldByOthers(1, "town") == Held{5});
  room.events.playerLeft(2);
  room.guests.erase(room.guests.begin() + 1);
  room.run();
  CHECK(room.events.heldByOthers(coop::HostPlayerId, "town").empty());
  CHECK(room.guests.at(0)->events.heldByOthers(1, "town").empty());
}
