#include <dwe/game/coop/battle.hpp>
#include <dwe/game/coop/link.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/loopback.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace dwe::game::coop;
namespace net = dwe::net;

namespace {

Identity identity() {
  return {.protocol = ProtocolVersion, .gameVersion = "1.2.3", .contentHash = "content-hash"};
}

template <typename T>
void roundTrip(const T& value) {
  const auto bytes = encode(Message{value});
  const auto decoded = decode(bytes);
  REQUIRE(decoded);
  REQUIRE(decoded->has_value());
  CHECK(std::get<T>(**decoded) == value);
}

BattleFighterProfile fighter() {
  return {.species = "agumon",
          .stats = {1200, 800, 150, 120, 100, 90},
          .hp = 1100,
          .mp = 600,
          .chargeMode = 1,
          .equipped = {0, 3, -1, 15},
          .discipline = 50,
          .tiredness = 10,
          .sick = false,
          .injured = true,
          .knownMoves = {"spit_fire", "sticky_net"}};
}

BattleSpoils spoils() {
  return {.hp = 900,
          .mp = 500,
          .statGains = {10, 0, 2, 1, 0, 3},
          .learnedMoves = {"heat_laser"},
          .injured = false,
          .care = {2, -1, 5, 1},
          .food = -3,
          .livesLost = 0,
          .elapsedMinutes = 3,
          .hourCrossed = true,
          .lifetimeHoursLost = 0,
          .departure = 0,
          .commandsLearned = {3, 8},
          .mpDiscountLearned = 10,
          .counters = {4, 1, 2, 0},
          .anyoneHit = true,
          .partnerHit = true,
          .frames = 600};
}

// The spoils of guest `guest`'s partner, as the host hands them to the referee's end.
std::vector<std::pair<PlayerId, BattleSpoils>> spoilsOf(PlayerId guest) {
  return {{guest, spoils()}};
}

BattleFrame frame(std::uint32_t battle, std::uint32_t number) {
  return {.battle = battle,
          .frame = number,
          .phase = BattlePhase::Fight,
          .command = 3,
          .menu = 0x0FFE,
          .bagAvailable = true,
          .finisherCharging = false,
          .fighters = {{.hp = 1000, .mp = 500, .maxHp = 1200, .maxMp = 800},
                       {.hp = 0, .mp = 10, .maxHp = 300, .maxMp = 100, .down = true}},
          .figures = {{.slot = PartnerFigureSlot,
                       .x = -1200,
                       .z = 3400,
                       .yaw = 0x4000,
                       .clip = 0x2E,
                       .starts = 7,
                       .frozen = false,
                       .pace = 16},
                      {.slot = 5, .x = 200, .z = -40, .yaw = 3, .clip = 1, .starts = 2}},
          .throws = {}};
}

BattleAction act(std::uint32_t battle, BattleActionKind kind, std::int32_t value = 0,
                 std::string item = {}) {
  return {.battle = battle, .kind = kind, .value = value, .item = std::move(item)};
}

BattleEnd ending(std::uint32_t battle, BattleOutcome outcome = BattleOutcome::Victory,
                 std::vector<std::int16_t> leftMap = {}, std::int32_t bits = 0,
                 std::optional<BattleSpoils> spoils = std::nullopt) {
  return {.battle = battle,
          .outcome = outcome,
          .leftMap = std::move(leftMap),
          .bits = bits,
          .spoils = std::move(spoils)};
}

// A host and a guest linked over a simulated network, both ready.
struct Linked {
  net::LoopbackNetwork network{{}};
  CoopLink host = CoopLink::host(network.link(0), identity());
  CoopLink guest = CoopLink::guest(network.link(1), identity(), "Guest");

  Linked() { pump(); }
  void pump() {
    for (int i = 0; i < 8; ++i) {
      network.advance(net::Millis{1});
      host.poll(network.now());
      guest.poll(network.now());
    }
  }
};

} // namespace

TEST_CASE("cooperative battle messages round trip") {
  roundTrip(BattleAsk{.ask = 3,
                      .mapId = "native_forest",
                      .enemyNpcs = {4, 6},
                      .fixedFight = true,
                      .predefined = false,
                      .fighter = fighter()});
  roundTrip(BattleBegin{.battle = 9,
                        .starter = GuestPlayerId,
                        .ask = 3,
                        .mapId = "native_forest",
                        .enemyNpcs = {4, 6},
                        .species = {"agumon", "goburimon", "goburimon"},
                        .fixedFight = false,
                        .allies = {}});
  roundTrip(BattleBegin{.battle = 10,
                        .starter = HostPlayerId,
                        .ask = 0,
                        .mapId = "native_forest",
                        .enemyNpcs = {1},
                        .species = {"gabumon", "kunemon"},
                        .fixedFight = true,
                        .allies = {}});
  roundTrip(BattleRefused{.ask = 3, .reason = BattleRefusal::Busy});
  roundTrip(BattleRefused{.ask = 4, .reason = BattleRefusal::NotHere});
  roundTrip(frame(9, 42));
  roundTrip(act(9, BattleActionKind::Command, 8));
  roundTrip(act(9, BattleActionKind::ThrowItem, 0, "medicine"));
  roundTrip(act(9, BattleActionKind::Cheer));
  roundTrip(act(9, BattleActionKind::CloseEnd));
  roundTrip(ending(9, BattleOutcome::Victory, std::vector<std::int16_t>{4, 6, 7}, 120, spoils()));
  roundTrip(ending(10, BattleOutcome::Fled, std::vector<std::int16_t>{}, 0));
}

TEST_CASE("cooperative battle messages reject what the battle cannot be") {
  const auto rejects = [](const Message& message) {
    const auto decoded = decode(encode(message));
    CHECK_FALSE(decoded);
  };
  // No ask number, no map, no enemy, more enemies than a battle has, a negative slot.
  rejects(BattleAsk{.ask = 0, .mapId = "m", .enemyNpcs = {1}, .fighter = fighter()});
  rejects(BattleAsk{.ask = 1, .mapId = "", .enemyNpcs = {1}, .fighter = fighter()});
  rejects(BattleAsk{.ask = 1, .mapId = "m", .enemyNpcs = {}, .fighter = fighter()});
  rejects(BattleAsk{.ask = 1, .mapId = "m", .enemyNpcs = {1, 2, 3, 4}, .fighter = fighter()});
  rejects(BattleAsk{.ask = 1, .mapId = "m", .enemyNpcs = {-2}, .fighter = fighter()});
  // A fighter without a species, with a move slot past its row, with an unknown charge mode.
  BattleFighterProfile broken = fighter();
  broken.species.clear();
  rejects(BattleAsk{.ask = 1, .mapId = "m", .enemyNpcs = {1}, .fighter = broken});
  broken = fighter();
  broken.equipped[0] = 16;
  rejects(BattleAsk{.ask = 1, .mapId = "m", .enemyNpcs = {1}, .fighter = broken});
  broken = fighter();
  broken.chargeMode = 3;
  rejects(BattleAsk{.ask = 1, .mapId = "m", .enemyNpcs = {1}, .fighter = broken});
  // A begin whose species do not match its fighters, or a guest's begin with no ask.
  rejects(BattleBegin{.battle = 1,
                      .starter = HostPlayerId,
                      .mapId = "m",
                      .enemyNpcs = {1},
                      .species = {"a"},
                      .fixedFight = false,
                      .allies = {}});
  rejects(BattleBegin{.battle = 1,
                      .starter = GuestPlayerId,
                      .ask = 0,
                      .mapId = "m",
                      .enemyNpcs = {1},
                      .species = {"a", "b"},
                      .fixedFight = false,
                      .allies = {}});
  rejects(BattleBegin{.battle = 1,
                      .starter = HostPlayerId,
                      .ask = 5,
                      .mapId = "m",
                      .enemyNpcs = {1},
                      .species = {"a", "b"},
                      .fixedFight = false,
                      .allies = {}});
  // A frame of no battle, a command past the strip's.
  rejects(frame(0, 1));
  BattleFrame strange = frame(1, 1);
  strange.command = 12;
  rejects(strange);
  strange = frame(1, 1);
  strange.figures.resize(MaxBattleFigures + 1);
  rejects(strange);
  // A command that is none, an item with no id.
  rejects(act(1, BattleActionKind::Command, 0));
  rejects(act(1, BattleActionKind::ThrowItem));
  rejects(BattleRefused{.ask = 0});
  // An unknown action kind on the wire.
  auto bytes = encode(Message{act(1, BattleActionKind::Cheer)});
  bytes[5] = 9;
  CHECK_FALSE(decode(bytes));
}

TEST_CASE("a full battle frame fits one unreliable datagram") {
  BattleFrame full = frame(1, 1);
  full.fighters.assign(MaxBattleFighters, {.hp = 1, .mp = 1, .maxHp = 1, .maxMp = 1});
  full.figures.assign(MaxBattleFigures, {.slot = 300, .x = -999'999, .z = 999'999});
  const auto bytes = encode(Message{full});
  CHECK(bytes.size() + 1 <= net::MaxDatagram);
  roundTrip(full);
}

TEST_CASE("each side sends only its own battle messages") {
  Linked linked;
  REQUIRE(linked.host.phase() == LinkPhase::Ready);
  REQUIRE(linked.guest.phase() == LinkPhase::Ready);
  // The guest asks and acts; it never tells the host about a battle.
  CHECK(linked.guest.send(
      Message{BattleAsk{.ask = 1, .mapId = "m", .enemyNpcs = {1}, .fighter = fighter()}}));
  CHECK(linked.guest.send(Message{act(1, BattleActionKind::Cheer)}));
  CHECK(linked.guest.send(Message{BattleOffer{.available = true, .fighter = fighter()}}));
  CHECK_FALSE(linked.guest.send(Message{frame(1, 1)}));
  CHECK_FALSE(linked.guest.send(Message{ending(1, BattleOutcome::Victory, {}, 0)}));
  // The host tells; it never asks.
  CHECK(linked.host.send(Message{frame(1, 1)}));
  CHECK(linked.host.send(Message{BattleRefused{.ask = 1}}));
  CHECK_FALSE(linked.host.send(
      Message{BattleAsk{.ask = 1, .mapId = "m", .enemyNpcs = {1}, .fighter = fighter()}}));
  CHECK_FALSE(linked.host.send(Message{act(1, BattleActionKind::Cheer)}));
  CHECK_FALSE(linked.host.send(Message{BattleOffer{.available = true, .fighter = fighter()}}));
}

TEST_CASE("the referee runs one battle at a time and pulls the second player in as helper") {
  BattleReferee referee;
  referee.guestJoined(GuestPlayerId);
  const BattleAsk ask{.ask = 4, .mapId = "forest", .enemyNpcs = {2}, .fighter = fighter()};
  CHECK(referee.judge(ask, "forest", false) == std::nullopt);
  CHECK(referee.judge(ask, "beach", false) == BattleRefusal::NotHere);
  // The host's own battle started first: the guest's ask waits for nothing, it is refused, and the
  // guest helps in the host's battle.
  CHECK(referee.judge(ask, "forest", true) == BattleRefusal::Busy);
  const BattleBegin hosts =
      referee.begin(HostPlayerId, 0, "forest", {3}, {"gabumon", "kunemon"}, false);
  CHECK(hosts.battle == 1);
  CHECK(hosts.ask == 0);
  CHECK(referee.judge(ask, "forest", false) == BattleRefusal::Busy);
  CHECK(referee.roleOf(HostPlayerId) == BattleRole::Starter);
  CHECK(referee.roleOf(GuestPlayerId) == BattleRole::Helper);
  // A helper throws its own items and cheers; it neither commands nor closes the end box.
  CHECK(referee.allows(GuestPlayerId, act(1, BattleActionKind::ThrowItem, 0, "meat")));
  CHECK(referee.allows(GuestPlayerId, act(1, BattleActionKind::Cheer)));
  CHECK_FALSE(referee.allows(GuestPlayerId, act(1, BattleActionKind::Command, 2)));
  CHECK_FALSE(referee.allows(GuestPlayerId, act(1, BattleActionKind::CloseEnd)));
  CHECK_FALSE(referee.allows(GuestPlayerId, act(2, BattleActionKind::Cheer)));
  // A guest the referee does not know (not joined) is not heard.
  CHECK_FALSE(referee.allows(2, act(1, BattleActionKind::Cheer)));
  // The host's battle gives the guest nothing back.
  const auto ends = referee.end(BattleOutcome::Victory, {3}, 50, spoilsOf(GuestPlayerId));
  REQUIRE(ends.size() == 1);
  const BattleEnd& end = ends.front().end;
  CHECK(ends.front().to == GuestPlayerId);
  CHECK(end.battle == 1);
  CHECK(end.bits == 50);
  CHECK_FALSE(end.spoils);
  CHECK_FALSE(referee.running());
  CHECK(referee.end(BattleOutcome::Victory, {}, 0, {}).empty());
}

TEST_CASE("the guest's battle takes its commands and gives its partner the spoils") {
  BattleReferee referee;
  referee.guestJoined(GuestPlayerId);
  const BattleBegin begin =
      referee.begin(GuestPlayerId, 7, "forest", {2, 5}, {"agumon", "a", "b"}, false);
  CHECK(begin.starter == GuestPlayerId);
  CHECK(begin.ask == 7);
  CHECK(referee.roleOf(GuestPlayerId) == BattleRole::Starter);
  CHECK(referee.roleOf(HostPlayerId) == BattleRole::Helper);
  CHECK(referee.allows(GuestPlayerId, act(begin.battle, BattleActionKind::Command, 8)));
  CHECK(referee.allows(GuestPlayerId, act(begin.battle, BattleActionKind::CloseEnd)));
  CHECK(referee.starterPresent());
  const auto ends = referee.end(BattleOutcome::Defeat, {}, 0, spoilsOf(GuestPlayerId));
  REQUIRE(ends.size() == 1);
  REQUIRE(ends.front().end.spoils);
  CHECK(ends.front().end.spoils->hp == 900);
}

TEST_CASE("a guest that drops mid-battle leaves its partner fighting on its own") {
  BattleReferee referee;
  referee.guestJoined(GuestPlayerId);
  const BattleBegin begin = referee.begin(GuestPlayerId, 1, "forest", {2}, {"agumon", "a"}, false);
  referee.guestLeft(GuestPlayerId);
  CHECK_FALSE(referee.starterPresent());
  // Nothing more of the guest counts, and nothing is sent to it.
  CHECK_FALSE(referee.allows(GuestPlayerId, act(begin.battle, BattleActionKind::Command, 2)));
  CHECK(referee.end(BattleOutcome::Victory, {2}, 30, spoilsOf(GuestPlayerId)).empty());
  // A host battle while the guest is away goes on with its starter there.
  referee.begin(HostPlayerId, 0, "forest", {2}, {"gabumon", "a"}, false);
  CHECK(referee.starterPresent());
  referee.guestJoined(GuestPlayerId);
  CHECK(referee.present(GuestPlayerId));
}

TEST_CASE("the mirror follows the host's battle and keeps only the newest frame") {
  BattleMirror mirror;
  const BattleAsk ask = mirror.ask("forest", {2}, false, true, fighter());
  CHECK(ask.ask == 1);
  CHECK(ask.predefined);
  CHECK(mirror.pendingAsk() == 1U);
  CHECK_FALSE(mirror.action(BattleActionKind::Cheer));

  // The host's own battle began first: the guest watches it as its helper, and its ask is
  // refused afterwards.
  mirror.receive(BattleBegin{.battle = 3,
                             .starter = HostPlayerId,
                             .ask = 0,
                             .mapId = "forest",
                             .enemyNpcs = {4},
                             .species = {"gabumon", "kunemon"},
                             .fixedFight = false,
                             .allies = {}});
  REQUIRE(mirror.takeBegan());
  CHECK(mirror.pendingAsk() == 1U);
  CHECK(mirror.role() == BattleRole::Helper);
  CHECK(mirror.action(BattleActionKind::ThrowItem, 0, "meat"));
  CHECK_FALSE(mirror.action(BattleActionKind::Command, 2));
  mirror.receive(BattleRefused{.ask = 9, .reason = BattleRefusal::Busy});
  CHECK_FALSE(mirror.takeRefused());
  mirror.receive(BattleRefused{.ask = 1, .reason = BattleRefusal::Busy});
  const auto refused = mirror.takeRefused();
  REQUIRE(refused);
  CHECK(refused->reason == BattleRefusal::Busy);
  CHECK_FALSE(mirror.pendingAsk());

  mirror.receive(frame(3, 10));
  mirror.receive(frame(3, 8));
  mirror.receive(frame(2, 50));
  REQUIRE(mirror.frame());
  CHECK(mirror.frame()->frame == 10);
  mirror.receive(frame(3, 11));
  CHECK(mirror.frame()->frame == 11);

  // An end for another battle changes nothing; the host's battle's end brings no spoils.
  mirror.receive(ending(2, BattleOutcome::Victory, {}, 0));
  CHECK_FALSE(mirror.takeEnded());
  mirror.receive(ending(3, BattleOutcome::Victory, std::vector<std::int16_t>{4}, 0, spoils()));
  const auto ended = mirror.takeEnded();
  REQUIRE(ended);
  CHECK_FALSE(ended->spoils);
  CHECK(ended->leftMap == std::vector<std::int16_t>{4});
  CHECK_FALSE(mirror.running());
  CHECK_FALSE(mirror.frame());
}

TEST_CASE("the guest's own battle answers its ask and brings its spoils") {
  BattleMirror mirror;
  const BattleAsk ask = mirror.ask("forest", {2}, false, false, fighter());
  mirror.receive(BattleBegin{.battle = 5,
                             .starter = GuestPlayerId,
                             .ask = ask.ask,
                             .mapId = "forest",
                             .enemyNpcs = {2},
                             .species = {"agumon", "kunemon"},
                             .fixedFight = false,
                             .allies = {}});
  CHECK_FALSE(mirror.pendingAsk());
  CHECK(mirror.role() == BattleRole::Starter);
  const auto command = mirror.action(BattleActionKind::Command, 8);
  REQUIRE(command);
  CHECK(command->battle == 5);
  mirror.receive(ending(5, BattleOutcome::Victory, {}, 0, spoils()));
  const auto ended = mirror.takeEnded();
  REQUIRE(ended);
  REQUIRE(ended->spoils);
  CHECK(ended->spoils->learnedMoves == std::vector<std::string>{"heat_laser"});
}

TEST_CASE("the host leaving drops the guest's ask and battle") {
  BattleMirror mirror;
  const BattleAsk ask = mirror.ask("forest", {2}, false, false, fighter());
  const BattleMirror::Dropped asked = mirror.hostLeft();
  CHECK(asked.ask == ask.ask);
  CHECK_FALSE(asked.battle);

  mirror.receive(BattleBegin{.battle = 1,
                             .starter = HostPlayerId,
                             .mapId = "forest",
                             .enemyNpcs = {2},
                             .species = {"gabumon", "kunemon"},
                             .fixedFight = false,
                             .allies = {}});
  const BattleMirror::Dropped fighting = mirror.hostLeft();
  CHECK_FALSE(fighting.ask);
  CHECK(fighting.battle);
  CHECK_FALSE(mirror.running());
  CHECK_FALSE(mirror.takeBegan());
}

TEST_CASE("a co-op battle runs over the link from the guest's ask to its end") {
  Linked linked;
  BattleReferee referee;
  referee.guestJoined(GuestPlayerId);
  BattleMirror mirror;
  const auto deliverToHost = [&]() -> std::optional<Message> {
    linked.guest.poll(linked.network.now());
    linked.network.advance(net::Millis{1});
    linked.host.poll(linked.network.now());
    return linked.host.receive();
  };
  const auto deliverToGuest = [&]() -> std::optional<Message> {
    linked.host.poll(linked.network.now());
    linked.network.advance(net::Millis{1});
    linked.guest.poll(linked.network.now());
    return linked.guest.receive();
  };

  REQUIRE(linked.guest.send(Message{mirror.ask("forest", {2}, false, false, fighter())}));
  const auto asked = deliverToHost();
  REQUIRE(asked);
  const auto& ask = std::get<BattleAsk>(*asked);
  CHECK(ask.fighter == fighter());
  REQUIRE_FALSE(referee.judge(ask, "forest", false));
  const BattleBegin begin = referee.begin(GuestPlayerId, ask.ask, ask.mapId, ask.enemyNpcs,
                                          {ask.fighter.species, "kunemon"}, ask.fixedFight);
  REQUIRE(linked.host.send(Message{begin}));
  REQUIRE(linked.host.send(Message{frame(begin.battle, 1)}));
  for (int i = 0; i < 2; ++i) {
    const auto told = deliverToGuest();
    REQUIRE(told);
    std::visit(
        [&](const auto& body) {
          using T = std::decay_t<decltype(body)>;
          if constexpr (std::is_same_v<T, BattleBegin> || std::is_same_v<T, BattleFrame>) {
            mirror.receive(body);
          }
        },
        *told);
  }
  REQUIRE(mirror.takeBegan());
  REQUIRE(mirror.frame());

  const auto command = mirror.action(BattleActionKind::Command, 2);
  REQUIRE(command);
  REQUIRE(linked.guest.send(Message{*command}));
  const auto acted = deliverToHost();
  REQUIRE(acted);
  CHECK(referee.allows(GuestPlayerId, std::get<BattleAction>(*acted)));

  const auto ends = referee.end(BattleOutcome::Victory, {2}, 40, spoilsOf(GuestPlayerId));
  REQUIRE(ends.size() == 1);
  REQUIRE(linked.host.send(Message{ends.front().end}));
  const auto ended = deliverToGuest();
  REQUIRE(ended);
  mirror.receive(std::get<BattleEnd>(*ended));
  const auto taken = mirror.takeEnded();
  REQUIRE(taken);
  REQUIRE(taken->spoils);
  CHECK(*taken->spoils == spoils());
}

TEST_CASE("both partners fight: the begin names the allies and their figures travel") {
  roundTrip(BattleOffer{.available = true, .fighter = fighter()});
  roundTrip(BattleOffer{.available = false, .fighter = fighter()});
  roundTrip(BattleBegin{.battle = 11,
                        .starter = HostPlayerId,
                        .ask = 0,
                        .mapId = "native_forest",
                        .enemyNpcs = {4},
                        .species = {"agumon", "goburimon", "gabumon"},
                        .fixedFight = false,
                        .allies = {GuestPlayerId}});
  BattleFrame withAlly = frame(11, 3);
  withAlly.figures.push_back({.slot = allyFigureSlot(GuestPlayerId), .x = 10, .z = 20});
  roundTrip(withAlly);
  BattleSpoils ranOff = spoils();
  ranOff.departure = 1;
  roundTrip(ending(11, BattleOutcome::Victory, {}, 50, ranOff));

  const auto rejects = [](const Message& message) {
    const auto decoded = decode(encode(message));
    CHECK_FALSE(decoded);
  };
  // An ally that is the starter, or without its species.
  rejects(BattleBegin{.battle = 1,
                      .starter = HostPlayerId,
                      .mapId = "m",
                      .enemyNpcs = {1},
                      .species = {"a", "b", "c"},
                      .fixedFight = false,
                      .allies = {HostPlayerId}});
  rejects(BattleBegin{.battle = 1,
                      .starter = HostPlayerId,
                      .mapId = "m",
                      .enemyNpcs = {1},
                      .species = {"a", "b"},
                      .fixedFight = false,
                      .allies = {GuestPlayerId}});
  // A figure below the last player's ally slot, a departure past knocked out.
  BattleFrame low = frame(11, 4);
  low.figures.push_back({.slot = static_cast<std::int16_t>(LowestFigureSlot - 1)});
  rejects(low);
  BattleSpoils unknown = spoils();
  unknown.departure = 3;
  rejects(ending(11, BattleOutcome::Victory, {}, 0, unknown));
}

TEST_CASE("the guest's partner joins the host's battle when it offers it, and commands it") {
  BattleReferee referee;
  referee.guestJoined(GuestPlayerId);
  CHECK_FALSE(referee.offered(GuestPlayerId));
  referee.receive(GuestPlayerId, BattleOffer{.available = true, .fighter = fighter()});
  REQUIRE(referee.offered(GuestPlayerId));
  CHECK(referee.offered(GuestPlayerId)->species == "agumon");
  referee.receive(GuestPlayerId, BattleOffer{.available = false, .fighter = fighter()});
  CHECK_FALSE(referee.offered(GuestPlayerId));

  const BattleBegin begin = referee.begin(HostPlayerId, 0, "forest", {2},
                                          {"gabumon", "a", "agumon"}, false, {GuestPlayerId});
  CHECK(begin.allies == std::vector<PlayerId>{GuestPlayerId});
  CHECK(referee.roleOf(GuestPlayerId) == BattleRole::Ally);
  CHECK(referee.roleOf(HostPlayerId) == BattleRole::Starter);
  // An ally commands its own partner and helps; the end box is the starter's.
  CHECK(referee.allows(GuestPlayerId, act(begin.battle, BattleActionKind::Command, 8)));
  CHECK(referee.allows(GuestPlayerId, act(begin.battle, BattleActionKind::Cheer)));
  CHECK_FALSE(referee.allows(GuestPlayerId, act(begin.battle, BattleActionKind::CloseEnd)));
  const auto ends = referee.end(BattleOutcome::Victory, {2}, 40, spoilsOf(GuestPlayerId));
  REQUIRE(ends.size() == 1);
  REQUIRE(ends.front().end.spoils);

  // A guest that leaves takes its offer with it.
  referee.receive(GuestPlayerId, BattleOffer{.available = true, .fighter = fighter()});
  referee.guestLeft(GuestPlayerId);
  CHECK_FALSE(referee.offered(GuestPlayerId));
}

TEST_CASE("the mirror sends its offer only when it changes, and knows it is an ally") {
  BattleMirror mirror;
  REQUIRE(mirror.offer(true, fighter()));
  CHECK_FALSE(mirror.offer(true, fighter()));
  BattleFighterProfile hurt = fighter();
  hurt.hp = 10;
  REQUIRE(mirror.offer(true, hurt));
  REQUIRE(mirror.offer(false, hurt));
  CHECK_FALSE(mirror.offer(false, hurt));

  mirror.receive(BattleBegin{.battle = 4,
                             .starter = HostPlayerId,
                             .ask = 0,
                             .mapId = "forest",
                             .enemyNpcs = {2},
                             .species = {"gabumon", "kunemon", "agumon"},
                             .fixedFight = false,
                             .allies = {GuestPlayerId}});
  CHECK(mirror.role() == BattleRole::Ally);
  CHECK(mirror.action(BattleActionKind::Command, 2));
  CHECK_FALSE(mirror.action(BattleActionKind::CloseEnd));
  mirror.receive(ending(4, BattleOutcome::Victory, {}, 0, spoils()));
  const auto ended = mirror.takeEnded();
  REQUIRE(ended);
  // Its partner fought: the spoils are its own.
  CHECK(ended->spoils);
  // The host gone, the offer is sent again on the next link.
  static_cast<void>(mirror.hostLeft());
  CHECK(mirror.offer(false, hurt));
}

// docs/architecture/coop-combat.md §4.1: every guest there and free fights as an ally.
TEST_CASE("every guest there joins as an ally and gets only its own spoils") {
  BattleReferee referee;
  for (PlayerId guest = GuestPlayerId; validPlayer(guest); ++guest) {
    referee.guestJoined(guest);
    BattleFighterProfile offered = fighter();
    offered.species = "guest_" + std::to_string(guest);
    referee.receive(guest, BattleOffer{.available = true, .fighter = offered});
  }
  CHECK(referee.guests() == std::vector<PlayerId>{1, 2, 3});
  REQUIRE(referee.offered(3));
  CHECK(referee.offered(3)->species == "guest_3");
  // Player 2 started it; the host's partner and the other guests' fight beside its own.
  const BattleBegin begin =
      referee.begin(2, 9, "forest", {4}, {"a", "foe", "b", "c", "d"}, false, {HostPlayerId, 1, 3});
  CHECK(begin.starter == 2);
  CHECK(begin.ask == 9);
  CHECK(referee.roleOf(2) == BattleRole::Starter);
  CHECK(referee.roleOf(1) == BattleRole::Ally);
  CHECK(referee.roleOf(3) == BattleRole::Ally);
  CHECK(referee.roleOf(HostPlayerId) == BattleRole::Ally);
  for (PlayerId guest = GuestPlayerId; validPlayer(guest); ++guest) {
    CHECK(referee.allows(guest, act(begin.battle, BattleActionKind::Command, 2)));
  }
  CHECK(referee.allows(2, act(begin.battle, BattleActionKind::CloseEnd)));
  CHECK_FALSE(referee.allows(3, act(begin.battle, BattleActionKind::CloseEnd)));
  // Player 3 drops mid-battle: its partner fights on, it hears nothing more.
  referee.guestLeft(3);
  CHECK_FALSE(referee.allows(3, act(begin.battle, BattleActionKind::Cheer)));
  CHECK_FALSE(referee.offered(3));
  std::vector<std::pair<PlayerId, BattleSpoils>> spoiled;
  for (PlayerId guest = GuestPlayerId; validPlayer(guest); ++guest) {
    BattleSpoils own = spoils();
    own.hp = 100 * guest;
    spoiled.emplace_back(guest, own);
  }
  const auto ends = referee.end(BattleOutcome::Victory, {4}, 40, spoiled);
  REQUIRE(ends.size() == 2);
  CHECK(ends[0].to == 1);
  CHECK(ends[1].to == 2);
  REQUIRE(ends[0].end.spoils);
  REQUIRE(ends[1].end.spoils);
  CHECK(ends[0].end.spoils->hp == 100);
  CHECK(ends[1].end.spoils->hp == 200);
  CHECK(ends[0].end.bits == 40);
}

TEST_CASE("a guest that is not in the battle helps; its mirror knows its own id") {
  BattleMirror mirror{3};
  CHECK(mirror.self() == 3);
  mirror.receive(BattleBegin{.battle = 2,
                             .starter = HostPlayerId,
                             .ask = 0,
                             .mapId = "forest",
                             .enemyNpcs = {2},
                             .species = {"gabumon", "kunemon", "agumon"},
                             .fixedFight = false,
                             .allies = {GuestPlayerId}});
  CHECK(mirror.role() == BattleRole::Helper);
  CHECK_FALSE(mirror.action(BattleActionKind::Command, 2));
  CHECK(mirror.action(BattleActionKind::ThrowItem, 0, "meat"));
  mirror.receive(ending(2, BattleOutcome::Victory, {}, 0, spoils()));
  const auto ended = mirror.takeEnded();
  REQUIRE(ended);
  CHECK_FALSE(ended->spoils);
  // Its own ask's begin makes it the starter.
  const BattleAsk ask = mirror.ask("forest", {2}, false, false, fighter());
  mirror.receive(BattleBegin{.battle = 3,
                             .starter = 3,
                             .ask = ask.ask,
                             .mapId = "forest",
                             .enemyNpcs = {2},
                             .species = {"agumon", "kunemon"},
                             .fixedFight = false,
                             .allies = {}});
  CHECK(mirror.role() == BattleRole::Starter);
  CHECK_FALSE(mirror.pendingAsk());
}

TEST_CASE("a battle frame carries each partner's revive countdown and an item's target") {
  BattleFrame withCountdown = frame(3, 4);
  withCountdown.fighters.front().down = true;
  withCountdown.fighters.front().reviveSeconds = 7;
  roundTrip(withCountdown);
  const auto rejects = [](const Message& message) {
    const auto decoded = decode(encode(message));
    CHECK_FALSE(decoded);
  };
  roundTrip(act(3, BattleActionKind::ThrowItem, 3, "meat"));
  rejects(act(3, BattleActionKind::ThrowItem, -1, "meat"));
  rejects(act(3, BattleActionKind::ThrowItem, static_cast<std::int32_t>(MaxBattleFighters) + 1,
              "meat"));
}

TEST_CASE("a guest takes a battle's spoils only when a battle could leave them") {
  BattleSpoils spoils;
  spoils.hp = 640;
  spoils.mp = 300;
  spoils.statGains = {12, 8, 3, 2, 1, 4};
  spoils.care = {2, -1, 5, 1};
  spoils.food = -10;
  spoils.livesLost = 1;
  spoils.elapsedMinutes = 3;
  spoils.counters = {4, 1, 0, 2};
  spoils.frames = 1800;
  CHECK(spoilsInRange(spoils));
  CHECK(spoilsInRange(BattleSpoils{}));

  const auto refused = [&](auto change) {
    BattleSpoils bad = spoils;
    change(bad);
    return !spoilsInRange(bad);
  };
  CHECK(refused([](BattleSpoils& bad) { bad.hp = 10'000; }));
  CHECK(refused([](BattleSpoils& bad) { bad.mp = -1; }));
  CHECK(refused([](BattleSpoils& bad) { bad.statGains[2] = 1'000'000; }));
  CHECK(refused([](BattleSpoils& bad) { bad.statGains[0] = -5; }));
  CHECK(refused([](BattleSpoils& bad) { bad.care[0] = 500; }));
  CHECK(refused([](BattleSpoils& bad) { bad.livesLost = -1; }));
  CHECK(refused([](BattleSpoils& bad) { bad.elapsedMinutes = 100'000; }));
  CHECK(refused([](BattleSpoils& bad) { bad.counters[1] = -3; }));
  CHECK(refused([](BattleSpoils& bad) { bad.frames = -1; }));
}

TEST_CASE("a battle frame carries the items thrown, and the guest gets them over the link") {
  BattleFrame thrown = frame(5, 9);
  thrown.throws = {{.serial = 3,
                    .thrower = 2,
                    .item = "meat",
                    .stage = BattleThrowStage::Flight,
                    .x = -300,
                    .z = 1200,
                    .height = 250,
                    .target = 4},
                   {.serial = 2,
                    .thrower = HostPlayerId,
                    .item = "medicine",
                    .stage = BattleThrowStage::Landed,
                    .x = 0,
                    .z = 0,
                    .height = 0,
                    .target = 0}};
  roundTrip(thrown);
  const auto rejects = [](BattleFrame bad) {
    const auto decoded = decode(encode(Message{std::move(bad)}));
    CHECK_FALSE(decoded);
  };
  BattleFrame bad = thrown;
  bad.throws.front().thrower = MaxPlayers;
  rejects(bad);
  bad = thrown;
  bad.throws.front().item.clear();
  rejects(bad);
  bad = thrown;
  bad.throws.front().target = MaxBattleFighters;
  rejects(bad);
  bad = thrown;
  bad.throws.assign(MaxBattleThrows + 1, thrown.throws.front());
  rejects(bad);
  auto bytes = encode(Message{frame(5, 9)});
  bytes.back() = 1;
  CHECK_FALSE(decode(bytes));
  BattleFrame full = frame(1, 1);
  full.fighters.assign(MaxBattleFighters, {.hp = 1, .mp = 1, .maxHp = 1, .maxMp = 1});
  full.figures.assign(MaxBattleFigures, {.slot = 300, .x = -999'999, .z = 999'999});
  full.throws.assign(MaxBattleThrows, {.serial = 0xFFFF,
                                       .thrower = 3,
                                       .item = std::string(64, 'i'),
                                       .stage = BattleThrowStage::Hand,
                                       .x = -999'999,
                                       .z = 999'999,
                                       .height = 999'999,
                                       .target = 6});
  CHECK(encode(Message{full}).size() + 1 <= net::MaxDatagram);
  roundTrip(full);
  Linked linked;
  REQUIRE(linked.host.send(Message{thrown}));
  linked.pump();
  CHECK(linked.guest.receive() == std::optional<Message>{Message{thrown}});
}

TEST_CASE("at the end box the allies and helpers wait on the starter, who alone closes it") {
  const BattleBegin begin{.battle = 6,
                          .starter = HostPlayerId,
                          .ask = 0,
                          .mapId = "forest",
                          .enemyNpcs = {2},
                          .species = {"gabumon", "kunemon", "agumon"},
                          .fixedFight = false,
                          .allies = {GuestPlayerId}};
  BattleMirror ally;
  BattleMirror helper{3};
  for (BattleMirror* mirror : {&ally, &helper}) {
    mirror->receive(begin);
    CHECK_FALSE(mirror->waitingFor());
    BattleFrame fight = frame(6, 1);
    mirror->receive(fight);
    CHECK_FALSE(mirror->waitingFor());
    BattleFrame end = frame(6, 2);
    end.phase = BattlePhase::End;
    mirror->receive(end);
    CHECK(mirror->waitingFor() == HostPlayerId);
    CHECK_FALSE(mirror->action(BattleActionKind::CloseEnd));
    mirror->receive(ending(6));
    CHECK_FALSE(mirror->waitingFor());
  }

  BattleMirror starter;
  const BattleAsk ask = starter.ask("forest", {2}, false, false, fighter());
  starter.receive(BattleBegin{.battle = 7,
                              .starter = GuestPlayerId,
                              .ask = ask.ask,
                              .mapId = "forest",
                              .enemyNpcs = {2},
                              .species = {"agumon", "kunemon"},
                              .fixedFight = false,
                              .allies = {}});
  BattleFrame end = frame(7, 1);
  end.phase = BattlePhase::End;
  starter.receive(end);
  CHECK_FALSE(starter.waitingFor());
  CHECK(starter.action(BattleActionKind::CloseEnd));
}
