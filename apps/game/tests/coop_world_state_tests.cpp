// The co-op shared world's interactions (#357): a host game and guest games, each with its own
// story and ground, talking through encoded messages as over the link.

#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/field/field_items.hpp>
#include <dwe/game/story/story_state.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "coop_world.hpp"
#include "coop_world_state.hpp"

using namespace dwe::app;
namespace coop = dwe::game::coop;
using dwe::game::field::Chest;
using dwe::game::field::ChestTrayOut;
using dwe::game::field::FieldItems;
using dwe::game::story::StoryState;
using dwe::net::Millis;

namespace {

constexpr std::int32_t ChestTrigger = 670;
constexpr std::int32_t MedalTrigger = 0x16c;
constexpr std::int32_t TalkFacingTrigger = 44;
constexpr std::int32_t FrozenVariable = 0;

const dwe::assets::ItemTable& itemTable() {
  static const dwe::assets::ItemTable items = [] {
    dwe::assets::ItemEntry meat;
    meat.id = "meat";
    dwe::assets::ItemEntry disk;
    disk.id = "medicine";
    return dwe::assets::ItemTable{std::vector{meat, disk}};
  }();
  return items;
}

bool sharedRule(const coop::WorldEdit& edit) {
  if (edit.kind == coop::WorldEditKind::Trigger) {
    return edit.key != MedalTrigger && edit.key != TalkFacingTrigger;
  }
  return edit.kind != coop::WorldEditKind::Variable || edit.key != FrozenVariable;
}

// One game of the session: its story, its ground and chests, on its map.
struct Game {
  StoryState story;
  FieldItems items{{.groundSlots = 11, .chestSlots = 2, .reachCells = 1}};
  std::string mapId = "room";
  std::uint32_t serial = 1;
  std::vector<coop::WorldEdit> mapEdits;
  // The item notice's take waiting for the host (a guest's game only sends it).
  FieldItemClaim claim;

  Game() {
    items.enterMap(mapId);
    REQUIRE(items.spawnChest({.item = itemTable().handleOf(itemTable().entries().front()),
                              .position = {-1235.0F, 0.0F, -1587.0F},
                              .rotation = 3072,
                              .trigger = ChestTrigger,
                              .taken = false,
                              .tray = 0}));
  }

  CoopWorldView view() {
    return {.story = &story,
            .items = &items,
            .itemTable = &itemTable(),
            .mapId = mapId,
            .mapSerial = serial,
            .settled = true,
            .mapEdits = std::exchange(mapEdits, {}),
            .shared = sharedRule,
            .claim = &claim};
  }

  // The player takes the chest as FieldItemsAdapter::takeChest does: not once it shows taken.
  bool takeChest() {
    if (items.chests()[0]->taken) {
      return false;
    }
    REQUIRE(story.setTrigger(ChestTrigger, true));
    items.markTaken(0);
    return true;
  }
  // The item notice confirms a take of the chest or of a ground slot (FieldItemsScreen).
  void claimTake(bool chest, std::size_t index) {
    claim = {.state = FieldItemClaim::State::Asked, .chest = chest, .index = index, .number = 0};
  }
  [[nodiscard]] FieldItemClaim::State claimed() const { return claim.state; }
  // Whether an item lies in a ground slot.
  [[nodiscard]] bool lying(std::size_t slot) const {
    const auto ground = items.ground();
    return slot < ground.size() && ground[slot].has_value();
  }
  [[nodiscard]] bool trigger(std::int32_t number) const {
    return story.trigger(number).value_or(false);
  }
};

// The wire: what a side sends arrives as the other side decodes it.
coop::Message wire(const coop::Message& message) {
  const auto decoded = coop::decode(coop::encode(message));
  REQUIRE(decoded);
  REQUIRE(decoded->has_value());
  return **decoded;
}

template <typename T>
std::vector<T> sent(const std::vector<CoopOutgoing>& out, coop::PlayerId to) {
  std::vector<T> found;
  for (const CoopOutgoing& message : out) {
    if (const auto* typed = std::get_if<T>(&message.message);
        typed != nullptr && message.to == to) {
      found.push_back(*typed);
    }
  }
  return found;
}

void deliver(const std::vector<CoopOutgoing>& out, coop::PlayerId to, CoopGuestWorld& guest) {
  for (const CoopOutgoing& message : out) {
    if (message.to == to) {
      guest.receive(wire(message.message));
    }
  }
}

void deliver(const std::vector<CoopOutgoing>& out, coop::PlayerId from, CoopHostWorld& host) {
  for (const CoopOutgoing& message : out) {
    host.receive(from, wire(message.message), Millis{0});
  }
}

// A host and two guests in session, each game agreed with the host (syncs taken).
struct Session {
  Game hostGame;
  Game firstGame;
  Game secondGame;
  CoopHostWorld host{"Host", "agumon"};
  CoopGuestWorld first{1};
  CoopGuestWorld second{2};

  Session() {
    REQUIRE(host.join("One", "gabumon", Millis{0}) == coop::PlayerId{1});
    REQUIRE(host.join("Two", "patamon", Millis{0}) == coop::PlayerId{2});
    exchange();
  }

  // One tick of every game: the host's share goes out, the guests take it and answer.
  CoopWorldShare exchange() {
    auto hostView = hostGame.view();
    CoopWorldShare fromHost = host.share(hostView);
    deliver(fromHost.out, 1, first);
    deliver(fromHost.out, 2, second);
    auto firstView = firstGame.view();
    lastFirst = first.share(firstView);
    auto secondView = secondGame.view();
    lastSecond = second.share(secondView);
    deliver(lastFirst.out, 1, host);
    deliver(lastSecond.out, 2, host);
    return fromHost;
  }

  CoopWorldShare lastFirst;
  CoopWorldShare lastSecond;
};

} // namespace

TEST_CASE("a chest trigger a guest's game sets is taken in the host's world and every guest's") {
  Session session;
  REQUIRE(session.firstGame.takeChest());
  // The guest's game asks; the host takes it, applies it and tells every guest.
  auto firstView = session.firstGame.view();
  const CoopWorldShare asked = session.first.share(firstView);
  const auto asks = sent<coop::WorldAsk>(asked.out, coop::HostPlayerId);
  REQUIRE(asks.size() == 1);
  CHECK(asks.front().edits ==
        std::vector<coop::WorldEdit>{
            {.kind = coop::WorldEditKind::Trigger, .key = ChestTrigger, .value = 1}});
  deliver(asked.out, 1, session.host);
  // A resend of the same ask is not applied again.
  deliver(asked.out, 1, session.host);
  auto hostView = session.hostGame.view();
  const CoopWorldShare change = session.host.share(hostView);
  CHECK(session.hostGame.trigger(ChestTrigger));
  // The host's chest shows taken at once, drawer out.
  CHECK(session.hostGame.items.chests()[0]->taken);
  CHECK(session.hostGame.items.chests()[0]->tray == ChestTrayOut);
  const auto toSecond = sent<coop::WorldChange>(change.out, 2);
  REQUIRE(toSecond.size() == 1);
  CHECK(toSecond.front().origin == 1);
  CHECK(sent<coop::WorldChange>(change.out, 1).size() == 1);
  const std::uint32_t revision = session.host.ledger().revision();

  deliver(change.out, 2, session.second);
  deliver(change.out, 2, session.second);
  auto secondView = session.secondGame.view();
  const CoopWorldShare taken = session.second.share(secondView);
  CHECK(session.secondGame.trigger(ChestTrigger));
  CHECK(session.secondGame.items.chests()[0]->taken);
  CHECK(session.second.worldRevision() == revision);
  // What came from the host is never sent back as the guest's own news.
  CHECK(sent<coop::WorldAsk>(taken.out, coop::HostPlayerId).empty());
  // Nothing more on the next ticks: the world agrees.
  const CoopWorldShare quiet = session.exchange();
  CHECK(sent<coop::WorldChange>(quiet.out, 1).empty());
  CHECK(sent<coop::WorldAsk>(session.lastFirst.out, coop::HostPlayerId).empty());
  CHECK(session.host.ledger().revision() == revision);
}

TEST_CASE("the host's own interactions reach every guest; a player's own facts stay home") {
  Session session;
  REQUIRE(session.hostGame.story.setTrigger(120, true));
  REQUIRE(session.hostGame.story.setVariable(7, 42));
  REQUIRE(session.hostGame.story.setTrigger(MedalTrigger, true));
  REQUIRE(session.hostGame.story.setVariable(FrozenVariable, 3));
  REQUIRE(session.firstGame.story.setTrigger(TalkFacingTrigger, true));
  const CoopWorldShare out = session.exchange();
  const auto changes = sent<coop::WorldChange>(out.out, 1);
  REQUIRE(changes.size() == 1);
  CHECK(changes.front().edits.size() == 2);
  CHECK(session.firstGame.trigger(120));
  CHECK(session.secondGame.story.variable(7).value() == 42);
  // The host's medal and freeze stay in its game; the guest's talk facing stays in its own.
  CHECK_FALSE(session.firstGame.trigger(MedalTrigger));
  CHECK(session.firstGame.story.variable(FrozenVariable).value() == 0);
  CHECK(sent<coop::WorldAsk>(session.lastFirst.out, coop::HostPlayerId).empty());
  CHECK_FALSE(session.hostGame.trigger(TalkFacingTrigger));
}

TEST_CASE("a guest that joins late or changes map gets the world as it is now") {
  Game hostGame;
  CoopHostWorld host{"Host", "agumon"};
  REQUIRE(hostGame.story.setTrigger(300, true));
  REQUIRE(hostGame.story.setVariable(200, 9));
  REQUIRE(hostGame.story.setTrigger(MedalTrigger, true));
  REQUIRE(hostGame.takeChest());
  REQUIRE(hostGame.items.spawn(itemTable().handleOf(itemTable().entries().back()),
                               {.column = 10, .row = 12}, 0.0F));
  auto hostView = hostGame.view();
  (void)host.share(hostView);

  // A guest whose copy of the world is older (its game started from an earlier save).
  Game late;
  REQUIRE(late.story.setTrigger(301, true));
  CoopGuestWorld guest{1};
  REQUIRE(host.join("Late", "gabumon", Millis{0}) == coop::PlayerId{1});
  hostView = hostGame.view();
  const CoopWorldShare joined = host.share(hostView);
  const auto parts = sent<coop::WorldSync>(joined.out, 1);
  CHECK(parts.size() == 2);
  CHECK(sent<coop::GroundItems>(joined.out, 1).size() == 1);
  deliver(joined.out, 1, guest);
  auto view = late.view();
  (void)guest.share(view);
  CHECK(late.trigger(300));
  CHECK_FALSE(late.trigger(301));
  CHECK(late.story.variable(200).value() == 9);
  CHECK(late.items.chests()[0]->taken);
  CHECK_FALSE(late.trigger(MedalTrigger));
  // On the host's map its ground is the host's.
  REQUIRE(late.items.ground().size() == 1);
  CHECK(late.items.ground()[0]->cell == dwe::assets::CollisionCell{.column = 10, .row = 12});

  // The host changes map: everyone gets the whole story again, and the new ground.
  hostGame.mapId = "next";
  hostGame.serial = 2;
  hostGame.items.enterMap("next");
  hostView = hostGame.view();
  const CoopWorldShare moved = host.share(hostView);
  CHECK(sent<coop::WorldSync>(moved.out, 1).size() == 2);
  const auto ground = sent<coop::GroundItems>(moved.out, 1);
  REQUIRE(ground.size() == 1);
  CHECK(ground.front().mapId == "next");
  CHECK(ground.front().items.empty());
}

TEST_CASE("a chest taken by two players within one round trip gives its item to one of them") {
  using State = FieldItemClaim::State;
  SUBCASE("the host and a guest: the host's take is at once, the guest's claim comes too late") {
    Session session;
    REQUIRE(session.hostGame.takeChest());
    session.firstGame.claimTake(true, 0);
    session.exchange();
    CHECK(session.firstGame.claimed() == State::Sent);
    const auto claims = sent<coop::ItemClaim>(session.lastFirst.out, coop::HostPlayerId);
    REQUIRE(claims.size() == 1);
    CHECK(claims.front().kind == coop::ClaimKind::Chest);
    CHECK(claims.front().key == ChestTrigger);
    const CoopWorldShare answered = session.exchange();
    const auto answers = sent<coop::ClaimAnswer>(answered.out, 1);
    REQUIRE(answers.size() == 1);
    CHECK_FALSE(answers.front().granted);
    CHECK(session.firstGame.claimed() == State::Refused);
    CHECK(session.firstGame.items.chests()[0]->taken);
  }
  SUBCASE("two guests: the first claim the host reads gets it, the other is refused") {
    Session session;
    session.firstGame.claimTake(true, 0);
    session.secondGame.claimTake(true, 0);
    session.exchange();
    session.exchange();
    CHECK(session.firstGame.claimed() == State::Granted);
    CHECK(session.secondGame.claimed() == State::Refused);
    // Every game shows it taken by the host's word; the host's own player finds it so.
    CHECK(session.hostGame.trigger(ChestTrigger));
    CHECK(session.firstGame.trigger(ChestTrigger));
    CHECK(session.secondGame.items.chests()[0]->taken);
    CHECK_FALSE(session.hostGame.takeChest());
    // A claim sent again is answered once.
    auto hostView = session.hostGame.view();
    session.host.receive(1,
                         coop::ItemClaim{.claim = 1,
                                         .kind = coop::ClaimKind::Chest,
                                         .key = ChestTrigger,
                                         .id = 0,
                                         .mapId = "room"},
                         Millis{0});
    CHECK(sent<coop::ClaimAnswer>(session.host.share(hostView).out, 1).empty());
  }
}

TEST_CASE("an item on the ground goes to the one player who picks it up, once") {
  using State = FieldItemClaim::State;
  Session session;
  REQUIRE(session.hostGame.items.spawn(itemTable().handleOf(itemTable().entries().front()),
                                       {.column = 4, .row = 5}, 0.0F));
  REQUIRE(session.hostGame.items.spawn(itemTable().handleOf(itemTable().entries().back()),
                                       {.column = 6, .row = 5}, 0.0F));
  session.exchange();
  REQUIRE(session.firstGame.lying(0));
  REQUIRE(session.secondGame.lying(1));
  CHECK(session.firstGame.items.ground()[0]->item ==
        itemTable().handleOf(itemTable().entries().front()));

  // The host and a guest take the same item within one round trip: the host's pick-up is at once
  // and the guest's claim is refused; the item stays on the guest's ground until the host's list.
  session.hostGame.items.remove(0);
  session.firstGame.claimTake(false, 0);
  session.exchange();
  CHECK(session.firstGame.claimed() == State::Sent);
  session.exchange();
  CHECK(session.firstGame.claimed() == State::Refused);
  CHECK_FALSE(session.firstGame.lying(0));

  // Two guests take the other one: the first claim gets it and it leaves every ground.
  session.firstGame.claimTake(false, 1);
  session.secondGame.claimTake(false, 1);
  session.exchange();
  const CoopWorldShare after = session.exchange();
  CHECK(session.firstGame.claimed() == State::Granted);
  CHECK(session.secondGame.claimed() == State::Refused);
  CHECK_FALSE(session.hostGame.lying(1));
  const auto groundAfter = sent<coop::GroundItems>(after.out, 2);
  REQUIRE_FALSE(groundAfter.empty());
  CHECK(groundAfter.back().items.empty());
  session.exchange();
  CHECK_FALSE(session.firstGame.lying(1));
  CHECK_FALSE(session.secondGame.lying(1));

  // Off the host's map a guest's ground is its own: its take needs no claim.
  session.firstGame.mapId = "elsewhere";
  session.firstGame.serial = 5;
  session.firstGame.items.enterMap("elsewhere");
  REQUIRE(session.firstGame.items.spawn(itemTable().handleOf(itemTable().entries().back()),
                                        {.column = 1, .row = 1}, 0.0F));
  session.exchange();
  CHECK(session.firstGame.lying(0));
  session.firstGame.claimTake(false, 0);
  session.exchange();
  CHECK(session.firstGame.claimed() == State::Granted);
  CHECK(sent<coop::ItemClaim>(session.lastFirst.out, coop::HostPlayerId).empty());
}

TEST_CASE("a guest's granted pick-up of the last item on the ground leaves its slot readable") {
  using State = FieldItemClaim::State;
  Session session;
  REQUIRE(session.hostGame.items.spawn(itemTable().handleOf(itemTable().entries().front()),
                                       {.column = 4, .row = 5}, 0.0F));
  session.exchange();
  REQUIRE(session.firstGame.items.groundAt(0));
  session.firstGame.claimTake(false, 0);
  session.exchange();
  session.exchange();
  REQUIRE(session.firstGame.claimed() == State::Granted);
  CHECK_FALSE(session.hostGame.items.groundAt(0));
  // The item notice reads its slot again once the host's word is in (FieldItemsScreen).
  REQUIRE_FALSE(session.firstGame.items.ground().empty());
  CHECK_FALSE(session.firstGame.items.ground()[0]);
  CHECK_FALSE(session.secondGame.items.ground()[0]);
}

TEST_CASE("a door a guest's script moves moves on the host's map and for the others there") {
  Session session;
  const coop::WorldEdit door{.kind = coop::WorldEditKind::MapObject, .key = 3, .value = 1};
  const coop::WorldEdit gate{
      .kind = coop::WorldEditKind::MapBlock, .key = 20, .value = 21, .width = 2, .height = 1};
  session.firstGame.mapEdits = {door, gate};
  session.exchange();
  const CoopWorldShare host = session.exchange();
  CHECK(host.mapEdits == std::vector<coop::WorldEdit>{door, gate});
  // The second guest on the map moves it too; the first already did it itself.
  CHECK(session.lastSecond.mapEdits == std::vector<coop::WorldEdit>{door, gate});
  CHECK(session.lastFirst.mapEdits.empty());

  // A door of a map the host is not on is nobody's.
  session.firstGame.mapId = "elsewhere";
  session.firstGame.mapEdits = {door};
  session.exchange();
  CHECK(session.exchange().mapEdits.empty());
}
