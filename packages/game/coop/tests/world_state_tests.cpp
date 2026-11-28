#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/coop/world_state.hpp>
#include <dwe/net/datagram.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace dwe::game::coop;

namespace {

template <typename T>
T roundTrip(const T& value) {
  const auto bytes = encode(Message{value});
  // Every world message travels reliably, under one datagram.
  CHECK(bytes.size() + 1 <= dwe::net::MaxDatagram);
  const auto decoded = decode(bytes);
  REQUIRE(decoded);
  REQUIRE(decoded->has_value());
  return std::get<T>(**decoded);
}

WorldEdit trigger(std::int32_t key, bool set = true) {
  return {.kind = WorldEditKind::Trigger, .key = key, .value = set ? 1 : 0};
}

// The vanilla story's own: trigger 44 and the medals stay with each player.
bool sharedRule(const WorldEdit& edit) {
  return edit.kind != WorldEditKind::Trigger || (edit.key != 44 && edit.key != 0x16c);
}

} // namespace

TEST_CASE("the shared world's messages round trip within one datagram") {
  const WorldAsk ask{
      .ask = 7,
      .seen = 41,
      .mapId = std::string(255, 'm'),
      .edits = std::vector<WorldEdit>(
          MaxWorldEdits,
          {.kind = WorldEditKind::MapBlock, .key = 3, .value = 4, .width = 2, .height = 1})};
  CHECK(roundTrip(ask) == ask);
  const WorldChange change{.revision = 9,
                           .origin = 2,
                           .ask = 7,
                           .mapId = "START",
                           .edits = {trigger(12),
                                     trigger(13, false),
                                     {.kind = WorldEditKind::Variable, .key = 5, .value = -3},
                                     {.kind = WorldEditKind::MapObject, .key = 4, .value = 1}}};
  CHECK(roundTrip(change) == change);

  std::vector<bool> triggers(MaxSyncTriggers);
  triggers[0] = triggers[9] = triggers[MaxSyncTriggers - 1] = true;
  const WorldSync sync{.revision = 3,
                       .part = 1,
                       .parts = 2,
                       .firstTrigger = 4096,
                       .triggers = triggers,
                       .firstVariable = 128,
                       .variables = std::vector<std::int32_t>(MaxSyncVariables, -2'000'000'000)};
  CHECK(roundTrip(sync) == sync);
  const WorldSync odd{.revision = 1,
                      .part = 0,
                      .parts = 1,
                      .firstTrigger = 0,
                      .triggers = {true, false, true},
                      .firstVariable = 0,
                      .variables = {}};
  CHECK(roundTrip(odd) == odd);

  GroundItems ground{.mapId = std::string(255, 'g'), .mapSerial = 4, .items = {}};
  for (std::uint8_t slot = 0; slot < MaxGroundEntries; ++slot) {
    ground.items.push_back({.id = 100U + slot,
                            .slot = slot,
                            .item = std::string(MaxGroundItemId, 'i'),
                            .column = -1,
                            .row = 99,
                            .x = -MaxWorldCoordinate,
                            .y = 0,
                            .z = MaxWorldCoordinate});
  }
  CHECK(roundTrip(ground) == ground);

  const ItemClaim chest{
      .claim = 3, .kind = ClaimKind::Chest, .key = 670, .id = 0, .mapId = std::string(255, 'c')};
  CHECK(roundTrip(chest) == chest);
  const ItemClaim lying{
      .claim = 4, .kind = ClaimKind::GroundItem, .key = 10, .id = 77, .mapId = "START"};
  CHECK(roundTrip(lying) == lying);
  CHECK(roundTrip(ClaimAnswer{.claim = 4, .granted = true}) ==
        ClaimAnswer{.claim = 4, .granted = true});
  CHECK(roundTrip(ClaimAnswer{.claim = 5, .granted = false}) ==
        ClaimAnswer{.claim = 5, .granted = false});

  const PoopPiles piles{.mapId = std::string(MaxPileMapText, 'p'),
                        .restart = true,
                        .piles =
                            std::vector<PileEntry>(MaxMapPiles, {.x = -3, .y = 99, .size = 40})};
  CHECK(roundTrip(piles) == piles);
  const PoopPiles between{.mapId = {}, .restart = true, .piles = {}};
  CHECK(roundTrip(between) == between);
  const PileAsk left{
      .ask = 2, .action = PileAction::Leave, .mapId = "START", .x = 5, .y = 7, .size = 12};
  CHECK(roundTrip(left) == left);
  const PileAsk eat{
      .ask = 3, .action = PileAction::Eat, .mapId = "START", .x = 5, .y = 7, .size = 0};
  CHECK(roundTrip(eat) == eat);
  CHECK(roundTrip(PileAnswer{.ask = 3, .granted = true, .size = 12}) ==
        PileAnswer{.ask = 3, .granted = true, .size = 12});
  CHECK(roundTrip(PileAnswer{.ask = 4, .granted = false, .size = 0}) ==
        PileAnswer{.ask = 4, .granted = false, .size = 0});
}

TEST_CASE("damaged shared world messages are refused") {
  auto bytes = encode(Message{
      WorldChange{.revision = 1, .origin = 0, .ask = 0, .mapId = "M", .edits = {trigger(3)}}});
  bytes.back() = 2; // a trigger's value is 0 or 1
  CHECK_FALSE(decode(bytes));
  CHECK_FALSE(decode(encode(
      Message{WorldChange{.revision = 0, .origin = 0, .ask = 0, .mapId = "M", .edits = {}}})));
  CHECK_FALSE(decode(encode(Message{WorldAsk{.ask = 1, .seen = 0, .mapId = {}, .edits = {}}})));
  CHECK_FALSE(decode(encode(Message{GroundItems{
      .mapId = "M",
      .mapSerial = 1,
      .items = {{.id = 1, .slot = 0, .item = "a"}, {.id = 2, .slot = 0, .item = "b"}}}})));
  CHECK_FALSE(decode(encode(Message{WorldSync{.revision = 1,
                                              .part = 1,
                                              .parts = 1,
                                              .firstTrigger = 0,
                                              .triggers = {},
                                              .firstVariable = 0,
                                              .variables = {}}})));
  CHECK_FALSE(decode(encode(
      Message{ItemClaim{.claim = 0, .kind = ClaimKind::Chest, .key = 1, .id = 0, .mapId = "M"}})));
  CHECK_FALSE(decode(encode(Message{
      ItemClaim{.claim = 1, .kind = ClaimKind::GroundItem, .key = 1, .id = 0, .mapId = "M"}})));
  CHECK_FALSE(decode(encode(
      Message{ItemClaim{.claim = 1, .kind = ClaimKind::Chest, .key = -1, .id = 0, .mapId = "M"}})));
  CHECK_FALSE(decode(encode(Message{ClaimAnswer{.claim = 0, .granted = true}})));

  CHECK_FALSE(decode(encode(
      Message{PoopPiles{.mapId = "M", .restart = false, .piles = {{.x = 1, .y = 1, .size = 0}}}})));
  CHECK_FALSE(decode(encode(Message{PoopPiles{.mapId = {}, .restart = false, .piles = {}}})));
  CHECK_FALSE(decode(
      encode(Message{PoopPiles{.mapId = "M",
                               .restart = false,
                               .piles = std::vector<PileEntry>(MaxMapPiles + 1, {.size = 1})}})));
  CHECK_FALSE(decode(encode(Message{
      PileAsk{.ask = 0, .action = PileAction::Eat, .mapId = "M", .x = 0, .y = 0, .size = 0}})));
  CHECK_FALSE(decode(encode(Message{
      PileAsk{.ask = 1, .action = PileAction::Leave, .mapId = "M", .x = 0, .y = 0, .size = 0}})));
  CHECK_FALSE(decode(encode(Message{
      PileAsk{.ask = 1, .action = PileAction::Eat, .mapId = {}, .x = 0, .y = 0, .size = 0}})));
  CHECK_FALSE(decode(encode(Message{PileAnswer{.ask = 1, .granted = true, .size = 0}})));
}

TEST_CASE("the host numbers its own shared changes and keeps a player's own facts out") {
  WorldLedger ledger{sharedRule};
  CHECK(ledger.record("START", {}).empty());
  const std::vector<WorldEdit> edits{trigger(44), trigger(0x16c)};
  CHECK(ledger.record("START", edits).empty());
  const auto changes = ledger.record("START", std::vector<WorldEdit>{trigger(44), trigger(12)});
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].revision == 1);
  CHECK(changes[0].origin == HostPlayerId);
  CHECK(changes[0].edits == std::vector<WorldEdit>{trigger(12)});
  // Many at once go out in several numbered messages.
  const auto many = ledger.record("START", std::vector<WorldEdit>(MaxWorldEdits + 1, trigger(1)));
  REQUIRE(many.size() == 2);
  CHECK(many[0].revision == 2);
  CHECK(many[1].revision == 3);
  CHECK(many[1].edits.size() == 1);
}

TEST_CASE("the host takes a guest's ask once, only the shared part, and only on its map") {
  WorldLedger ledger{sharedRule};
  const auto always = [](const WorldEdit&) { return true; };
  const WorldAsk ask{.ask = 1, .seen = 0, .mapId = "START", .edits = {trigger(12), trigger(44)}};

  CHECK(ledger.accept(1, ask, "START", false, always).verdict == AskVerdict::UnknownPlayer);
  CHECK(ledger.accept(HostPlayerId, ask, "START", true, always).verdict ==
        AskVerdict::UnknownPlayer);

  const auto taken = ledger.accept(1, ask, "START", true, always);
  CHECK(taken.verdict == AskVerdict::Accepted);
  REQUIRE(taken.change);
  CHECK(taken.change->origin == 1);
  CHECK(taken.change->ask == 1);
  CHECK(taken.change->revision == 1);
  CHECK(taken.change->edits == std::vector<WorldEdit>{trigger(12)});
  CHECK(taken.refused == std::vector<WorldEdit>{trigger(44)});

  // The same ask again (a resend) is never applied twice.
  CHECK(ledger.accept(1, ask, "START", true, always).verdict == AskVerdict::Stale);
  CHECK(ledger.revision() == 1);

  // What changes nothing on the host is not news.
  const auto none =
      ledger.accept(1, {.ask = 2, .seen = 0, .mapId = "START", .edits = {trigger(12)}}, "START",
                    true, [](const WorldEdit&) { return false; });
  CHECK(none.verdict == AskVerdict::Nothing);
  CHECK_FALSE(none.change);

  // A door on a map the session left is nobody's; the facts in the same ask still count.
  const WorldEdit door{.kind = WorldEditKind::MapObject, .key = 3, .value = 1};
  CHECK(
      ledger
          .accept(1, {.ask = 3, .seen = 0, .mapId = "OLD", .edits = {door}}, "START", true, always)
          .verdict == AskVerdict::WrongMap);
  const auto mixed =
      ledger.accept(1, {.ask = 4, .seen = 0, .mapId = "OLD", .edits = {door, trigger(20)}}, "START",
                    true, always);
  REQUIRE(mixed.change);
  CHECK(mixed.change->edits == std::vector<WorldEdit>{trigger(20)});

  // A guest that leaves and comes back with the same id starts its asks over.
  ledger.leave(1);
  CHECK(ledger
            .accept(1, {.ask = 1, .seen = 0, .mapId = "START", .edits = {trigger(21)}}, "START",
                    true, always)
            .verdict == AskVerdict::Accepted);
}

TEST_CASE("a guest applies each host change once and in order, and a sync covers the rest") {
  WorldReplica replica;
  const WorldChange first{.revision = 1, .origin = 0, .ask = 0, .mapId = "M", .edits = {}};
  const WorldChange second{.revision = 2, .origin = 1, .ask = 1, .mapId = "M", .edits = {}};
  CHECK(replica.take(first));
  CHECK_FALSE(replica.take(first));
  CHECK(replica.take(second));
  CHECK_FALSE(replica.take(first));

  // A sync at revision 5 in two parts: both apply, then changes up to 5 are covered by it.
  const auto parts = worldSyncParts(5, std::vector<bool>(800), std::vector<std::int32_t>(256));
  REQUIRE(parts.size() == 2);
  CHECK(parts[0].triggers.size() == 800);
  CHECK(parts[0].variables.size() == MaxSyncVariables);
  CHECK(parts[1].triggers.empty());
  CHECK(parts[1].firstVariable == static_cast<std::int32_t>(MaxSyncVariables));
  CHECK(replica.take(parts[0]));
  CHECK(replica.revision() == 2);
  CHECK(replica.take(parts[1]));
  CHECK(replica.revision() == 5);
  CHECK_FALSE(
      replica.take(WorldChange{.revision = 4, .origin = 0, .ask = 0, .mapId = {}, .edits = {}}));
  CHECK(replica.take(WorldChange{.revision = 6, .origin = 0, .ask = 0, .mapId = {}, .edits = {}}));
  // An older sync (sent before a change already taken) is dropped.
  CHECK_FALSE(replica.take(parts[1]));

  const auto asks = replica.ask("M", std::vector<WorldEdit>(MaxWorldEdits * 2, trigger(1)));
  REQUIRE(asks.size() == 2);
  CHECK(asks[0].ask == 1);
  CHECK(asks[1].ask == 2);
  CHECK(replica.ask("M", {}).empty());
}

TEST_CASE("a room reset overtakes what a guest asked before it saw the reset") {
  WorldLedger ledger{sharedRule};
  const auto always = [](const WorldEdit&) { return true; };
  ledger.newMapLoad();
  REQUIRE(ledger.revision() == 1);
  // The guest walks the trial: its step sets a path trigger the host takes.
  const auto step = ledger.accept(
      1, {.ask = 1, .seen = 1, .mapId = "ROOM", .edits = {trigger(161)}}, "ROOM", true, always);
  REQUIRE(step.verdict == AskVerdict::Accepted);
  // The host's game resets the room: a new load of the map and the path cleared.
  ledger.newMapLoad();
  const auto reset = ledger.record("ROOM", std::vector{trigger(161, false), trigger(162, false)});
  REQUIRE(reset.size() == 1);
  const std::uint32_t afterReset = reset.front().revision;
  // An ask the guest's game made before it saw the reset: its path and its gate are the old room's.
  const WorldEdit gate{.kind = WorldEditKind::MapObject, .key = 38, .value = 0};
  const auto stale =
      ledger.accept(1, {.ask = 2, .seen = 2, .mapId = "ROOM", .edits = {trigger(162), gate}},
                    "ROOM", true, always);
  CHECK(stale.verdict == AskVerdict::Overtaken);
  CHECK_FALSE(stale.change);
  CHECK(stale.overtaken == std::vector{trigger(162)});
  CHECK(ledger.revision() == afterReset);
  // Even a path trigger the reset found clear: it was stepped on in the old room.
  CHECK(ledger
            .accept(1, {.ask = 3, .seen = 2, .mapId = "ROOM", .edits = {trigger(170)}}, "ROOM",
                    true, always)
            .overtaken == std::vector{trigger(170)});
  // A guest's asks from the map the group left keep their facts: only the room's load is gone.
  CHECK(ledger
            .accept(1, {.ask = 4, .seen = 2, .mapId = "HALL", .edits = {trigger(171), gate}},
                    "ROOM", true, always)
            .change->edits == std::vector{trigger(171)});
  // Another guest's change it had not seen overtakes it too; its own earlier change never does.
  REQUIRE(ledger
              .accept(2, {.ask = 1, .seen = afterReset, .mapId = "ROOM", .edits = {trigger(165)}},
                      "ROOM", true, always)
              .change);
  CHECK(ledger
            .accept(1,
                    {.ask = 7, .seen = afterReset, .mapId = "ROOM", .edits = {trigger(165, false)}},
                    "ROOM", true, always)
            .verdict == AskVerdict::Overtaken);
  CHECK(ledger
            .accept(2,
                    {.ask = 2, .seen = afterReset, .mapId = "ROOM", .edits = {trigger(165, false)}},
                    "ROOM", true, always)
            .verdict == AskVerdict::Accepted);
  // Once the guest saw the reset its asks count again, and a fact nobody touched never waits.
  const auto fresh = ledger.accept(
      1, {.ask = 8, .seen = afterReset, .mapId = "ROOM", .edits = {trigger(162), gate}}, "ROOM",
      true, always);
  REQUIRE(fresh.verdict == AskVerdict::Accepted);
  CHECK(fresh.change->edits == std::vector{trigger(162), gate});
  const auto untouched =
      ledger.accept(1, {.ask = 9, .seen = afterReset, .mapId = "ROOM", .edits = {trigger(300)}},
                    "ROOM", true, always);
  CHECK(untouched.verdict == AskVerdict::Accepted);
}

TEST_CASE("a guest's asks say the newest revision it took") {
  WorldReplica replica;
  CHECK(replica.ask("M", std::vector{trigger(1)}).front().seen == 0);
  REQUIRE(
      replica.take(WorldChange{.revision = 5, .origin = 0, .ask = 0, .mapId = "M", .edits = {}}));
  CHECK(replica.ask("M", std::vector{trigger(1)}).front().seen == 5);
}
