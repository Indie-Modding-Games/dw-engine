// The partners' poop piles in co-op (multiplayer.md "Shared world interactions").

#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/partner/care_rules.hpp>
#include <dwe/game/partner/poop_piles.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/ruleset/random.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "coop_gameplay_bridge.hpp"
#include "coop_world.hpp"
#include "coop_world_state.hpp"
#include "packages/game/coop/tests/support/coop_room.hpp"

using namespace dwe::app;
namespace coop = dwe::game::coop;
using dwe::game::partner::PoopPile;
using dwe::game::partner::Tile;
using dwe::net::Millis;
using EatState = dwe::app::FieldPileShare::Eat::State;

namespace {

constexpr std::size_t Guests = 2;

dwe::game::partner::ToiletRules toilet() {
  dwe::game::partner::ToiletRules rules;
  rules.pilesPerMap = 16;
  rules.pilesTotal = 100;
  return rules;
}

struct PileGame {
  dwe::game::story::StoryState story;
  dwe::game::partner::PoopPiles piles{toilet()};
  dwe::ruleset::Random random{7};
  FieldPileShare share;
  std::string mapId = "room";
  std::uint32_t serial = 1;

  CoopWorldView view(bool guest) {
    return {.story = &story,
            .items = nullptr,
            .itemTable = nullptr,
            .mapId = mapId,
            .mapSerial = serial,
            .settled = true,
            .mapEdits = {},
            .shared = {},
            .claim = nullptr,
            .piles = &piles,
            .random = &random,
            .pileShare = guest ? &share : nullptr};
  }

  // As FieldPoop does: on its screen at once, and for the host.
  void leave(Tile tile, std::int32_t size) {
    const Tile placed = piles.placeAt(mapId, tile, size, random);
    share.left.push_back({.map = mapId, .tile = placed, .size = size});
  }
  void eat(Tile tile) {
    share.eat = {.state = EatState::Asked, .mapId = mapId, .tile = tile, .number = 0, .size = 0};
  }
  [[nodiscard]] bool shows(Tile tile) const { return piles.at(mapId, tile).has_value(); }
  [[nodiscard]] std::size_t count(const std::string& map) const { return piles.onMap(map).size(); }
};

// Each side's messages go through its game's bridge as the game loop routes them.
struct PileRoom {
  coop::test::CoopRoom room{{.guests = Guests}};
  std::array<NetworkFieldController, Guests * 2> controllers;
  std::vector<std::unique_ptr<CoopGameplayBridge>> hostSides;
  std::vector<std::unique_ptr<CoopGameplayBridge>> guestSides;
  CoopHostWorld host{"Host", "agumon"};
  PileGame hostGame;
  std::array<PileGame, Guests> games;
  std::vector<std::unique_ptr<CoopGuestWorld>> guests;

  explicit PileRoom(std::size_t joined = Guests) {
    REQUIRE(room.connect());
    for (std::size_t index = 0; index < Guests; ++index) {
      const auto id = static_cast<coop::PlayerId>(index + 1);
      hostSides.push_back(
          std::make_unique<CoopGameplayBridge>(room.host(id), controllers[index * 2]));
      guestSides.push_back(
          std::make_unique<CoopGameplayBridge>(room.guest(id), controllers[(index * 2) + 1]));
      guests.push_back(std::make_unique<CoopGuestWorld>(id));
    }
    for (std::size_t index = 0; index < joined; ++index) {
      join(index);
    }
    tick();
  }

  void join(std::size_t index) {
    REQUIRE(host.join("Guest", "gabumon", Millis{0}) == static_cast<coop::PlayerId>(index + 1));
  }

  void carry() {
    room.pump(16);
    for (std::size_t index = 0; index < Guests; ++index) {
      hostSides[index]->poll(Millis{0});
      guestSides[index]->poll(Millis{0});
    }
  }

  // One tick of every game: the host shares, the guests take it and share, the host takes theirs.
  void tick() {
    auto hostView = hostGame.view(false);
    for (const CoopOutgoing& out : host.share(hostView).out) {
      REQUIRE(hostSides.at(out.to - 1U)->send(out.message));
    }
    carry();
    for (std::size_t index = 0; index < Guests; ++index) {
      for (const coop::Message& message : guestSides[index]->takeWorldMessages()) {
        guests[index]->receive(message);
      }
      auto view = games[index].view(true);
      for (const CoopOutgoing& out : guests[index]->share(view).out) {
        REQUIRE(guestSides[index]->send(out.message));
      }
    }
    carry();
    for (std::size_t index = 0; index < Guests; ++index) {
      for (const coop::Message& message : hostSides[index]->takeWorldMessages()) {
        host.receive(static_cast<coop::PlayerId>(index + 1), message, Millis{0});
      }
    }
  }

  void ticks(int count) {
    for (int i = 0; i < count; ++i) {
      tick();
    }
  }
};

} // namespace

TEST_CASE("a pile the host's partner leaves shows in every game, on its map only") {
  PileRoom session;
  const Tile pile =
      session.hostGame.piles.place("room", {.x = 5, .y = 5}, 0, 8, session.hostGame.random);
  session.games[1].mapId = "lake";
  session.ticks(2);
  CHECK(session.games[0].shows(pile));
  CHECK(session.games[0].count("room") == 1);
  CHECK(session.games[1].count("lake") == 0);
  CHECK(session.games[1].piles.at("room", pile).has_value());
}

TEST_CASE("a pile a guest's partner leaves is the host's, and every other game shows it once") {
  PileRoom session;
  session.games[0].leave({.x = 4, .y = 4}, 10);
  CHECK(session.games[0].shows({.x = 4, .y = 4}));
  session.ticks(2);
  CHECK(session.hostGame.piles.at("room", {.x = 4, .y = 4}) ==
        PoopPile{.map = "room", .tile = {.x = 4, .y = 4}, .size = 10});
  CHECK(session.games[1].shows({.x = 4, .y = 4}));
  CHECK(session.games[0].count("room") == 1);
  CHECK(session.hostGame.count("room") == 1);
  // A second pile on that tile goes a step aside; every game ends with the host's two.
  session.games[1].leave({.x = 4, .y = 4}, 6);
  session.ticks(2);
  CHECK(session.hostGame.count("room") == 2);
  CHECK(session.games[0].piles.onMap("room") == session.hostGame.piles.onMap("room"));
  CHECK(session.games[1].piles.onMap("room") == session.hostGame.piles.onMap("room"));
}

TEST_CASE("two partners eat one pile at once: the first eats it, the other gets nothing") {
  PileRoom session;
  const Tile pile{.x = 6, .y = 6};
  session.hostGame.piles.placeAt("room", pile, 12, session.hostGame.random);
  session.tick();
  REQUIRE(session.games[0].shows(pile));
  REQUIRE(session.games[1].shows(pile));
  session.games[0].eat(pile);
  session.games[1].eat(pile);
  session.ticks(2);
  CHECK(session.games[0].share.eat.state == EatState::Granted);
  CHECK(session.games[0].share.eat.size == 12);
  CHECK(session.games[1].share.eat.state == EatState::Refused);
  CHECK_FALSE(session.hostGame.shows(pile));
  CHECK_FALSE(session.games[0].shows(pile));
  CHECK_FALSE(session.games[1].shows(pile));

  // The host's partner eats first (its game is the world): a guest's eat in the same round trip
  // finds the pile gone.
  session.hostGame.piles.placeAt("room", pile, 9, session.hostGame.random);
  session.tick();
  REQUIRE(session.games[0].shows(pile));
  session.games[0].eat(pile);
  REQUIRE(session.hostGame.piles.remove("room", pile) == 9);
  session.ticks(2);
  CHECK(session.games[0].share.eat.state == EatState::Refused);
  CHECK_FALSE(session.games[0].shows(pile));
}

TEST_CASE("a guest's pile ask sent twice is taken once") {
  PileRoom session;
  const coop::Message ask{coop::PileAsk{
      .ask = 1, .action = coop::PileAction::Leave, .mapId = "room", .x = 2, .y = 3, .size = 5}};
  REQUIRE(session.guestSides[0]->send(ask));
  REQUIRE(session.guestSides[0]->send(ask));
  session.carry();
  for (const coop::Message& message : session.hostSides[0]->takeWorldMessages()) {
    session.host.receive(coop::GuestPlayerId, message, Millis{0});
  }
  session.ticks(2);
  CHECK(session.hostGame.count("room") == 1);
  CHECK(session.games[1].count("room") == 1);
}

TEST_CASE("a guest that joins late gets every map's piles, and its stale ones go") {
  PileRoom session{1};
  session.hostGame.piles.placeAt("room", {.x = 1, .y = 1}, 4, session.hostGame.random);
  session.hostGame.piles.placeAt("lake", {.x = 2, .y = 2}, 5, session.hostGame.random);
  // The late guest's copy of the world is older: a pile the host's world no longer has.
  session.games[1].piles.placeAt("cave", {.x = 3, .y = 3}, 6, session.games[1].random);
  session.ticks(2);
  CHECK(session.games[1].count("room") == 0);
  CHECK(session.games[1].count("cave") == 1);
  session.join(1);
  session.ticks(2);
  CHECK(session.games[1].shows({.x = 1, .y = 1}));
  CHECK(session.games[1].piles.at("lake", {.x = 2, .y = 2}).has_value());
  CHECK(session.games[1].count("cave") == 0);
}

TEST_CASE("on a new map every guest gets the host's piles again") {
  PileRoom session;
  session.hostGame.piles.placeAt("lake", {.x = 8, .y = 8}, 7, session.hostGame.random);
  // A pile only this guest's game holds (its world went its own way).
  session.games[0].piles.placeAt("lake", {.x = 1, .y = 9}, 3, session.games[0].random);
  session.hostGame.mapId = "lake";
  session.hostGame.serial = 2;
  for (PileGame& game : session.games) {
    game.mapId = "lake";
    game.serial = 2;
  }
  session.ticks(2);
  CHECK(session.games[0].piles.onMap("lake") == session.hostGame.piles.onMap("lake"));
  CHECK(session.games[1].piles.onMap("lake") == session.hostGame.piles.onMap("lake"));
  CHECK_FALSE(session.games[0].shows({.x = 1, .y = 9}));
}
