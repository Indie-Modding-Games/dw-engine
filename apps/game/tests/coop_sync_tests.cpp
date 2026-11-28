// The shared world's sync harness (#359): a host and one to three guests, headless, each guest on
// a lossy, jittery loopback link of its own, every side with its own field. The players move, then
// stand; the diagnostics must see the losses and, once everyone stands, every side must hold every
// player where the host has it.
#include <dwe/game/field/field.hpp>
#include <dwe/net/loopback.hpp>
#include <dwe/scene/world.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "coop_gameplay_bridge.hpp"
#include "coop_snapshot.hpp"
#include "coop_snapshot_interpolation.hpp"
#include "coop_sync.hpp"
#include "coop_world.hpp"
#include "packages/game/field/tests/fixture.hpp"

using namespace dwe::app;
namespace coop = dwe::game::coop;
using dwe::game::field::Field;
using dwe::game::field::FieldInput;
using dwe::game::field::testing::originalRules;
using dwe::game::field::testing::roomMap;
using dwe::net::Millis;

namespace {

constexpr float TickSeconds = 1.0F / 60.0F;
constexpr Millis TickMillis{16};

coop::Identity identity() {
  return {.protocol = coop::ProtocolVersion, .gameVersion = "test", .contentHash = "content"};
}

dwe::assets::SpawnPoint at(float x, float z) {
  return {.position = {x, 0.0F, z}, .yaw = 0.0F};
}

// One guest: its own network to the host, its link on both ends, its field and its world.
struct Guest {
  coop::PlayerId id;
  dwe::net::LoopbackNetwork network;
  coop::CoopLink hostLink;
  coop::CoopLink guestLink;
  NetworkFieldController onHost;
  NetworkFieldController unused;
  CoopGameplayBridge hostSide;
  CoopGameplayBridge guestSide;
  dwe::scene::World world;
  Field field{world, originalRules()};
  CoopGuestWorld shared;
  CoopSync sync;

  Guest(coop::PlayerId player, dwe::net::LinkConditions conditions)
      : id{player}, network{conditions, 7U + player},
        hostLink{coop::CoopLink::host(network.link(0), identity(), "Host", {}, player)},
        guestLink{coop::CoopLink::guest(network.link(1), identity(), "Guest")},
        hostSide{hostLink, onHost}, guestSide{guestLink, unused}, shared{player} {
    field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  }
};

struct Session {
  dwe::scene::World world;
  Field field{world, originalRules()};
  CoopHostWorld host{"Host", ""};
  CoopSync sync;
  std::vector<std::unique_ptr<Guest>> guests;
  // Guests the host's snapshots do not reach (a one-way loss).
  std::set<coop::PlayerId> starved;
  std::vector<std::string> log;
  std::uint32_t worldTick = 0;
  std::uint64_t ticks = 0;
  Millis now{0};

  explicit Session(std::size_t players, dwe::net::LinkConditions conditions) {
    field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
    for (coop::PlayerId id = 1; id < players; ++id) {
      guests.push_back(std::make_unique<Guest>(id, conditions));
    }
    // The handshake on every link (lossy links retry their reliable lane).
    for (int step = 0; step < 400; ++step) {
      advance();
      for (auto& guest : guests) {
        guest->hostSide.poll(now);
        guest->guestSide.poll(now);
      }
      if (std::ranges::all_of(guests, [](const auto& guest) {
            return guest->hostLink.phase() == coop::LinkPhase::Ready &&
                   guest->guestLink.phase() == coop::LinkPhase::Ready;
          })) {
        break;
      }
    }
    for (auto& guest : guests) {
      REQUIRE(guest->hostLink.phase() == coop::LinkPhase::Ready);
      REQUIRE(guest->guestLink.localPlayer() == guest->id);
      REQUIRE(host.join("Guest", "", now, guest->id) == guest->id);
      REQUIRE(field.addPlayer(guest->id, at(300.0F * static_cast<float>(guest->id), 0.0F)));
    }
    sync.ask("on");
  }

  void advance() {
    now += TickMillis;
    for (auto& guest : guests) {
      guest->network.advance(TickMillis);
    }
  }

  // One 60 Hz tick on every side, in the game loop's order; `moving`: the players walk.
  // `stalled`: that side (0 the host) is busy loading and runs nothing this tick.
  void tick(bool moving, std::optional<coop::PlayerId> stalled = std::nullopt) {
    advance();
    ++ticks;
    const auto stepOf = [&](float angle) {
      return moving ? FieldInput{.stick = {std::cos(angle), std::sin(angle)},
                                 .run = true,
                                 .moveTarget = std::nullopt,
                                 .cancelTarget = false}
                    : FieldInput{};
    };
    // Each guest: what the host corrected, its own walk, its report.
    for (auto& guest : guests) {
      if (stalled == guest->id) {
        continue;
      }
      guest->guestSide.poll(now);
      for (const coop::Message& message : guest->guestSide.takeWorldMessages()) {
        guest->shared.receive(message);
      }
      if (const auto move = guest->shared.apply(guest->field, false)) {
        guest->field.placeAt(guest->field.playerEntity(), move->at.position, move->at.yaw);
      }
      guest->field.setInput(stepOf(static_cast<float>(guest->id) * 1.7F));
      guest->field.tick(TickSeconds);
      if (auto state = guest->shared.report(guest->field, moving, ticks)) {
        (void)guest->guestSide.send(*state);
      }
      const auto presented = guest->guestSide.presentedWorld(now);
      guest->sync.guestTick(guest->field, presented ? &*presented : nullptr, guest->id,
                            guest->guestSide, ticks);
    }
    // The host: the guests' states, its own walk, the corrections and the snapshots.
    if (stalled == 0) {
      return;
    }
    for (auto& guest : guests) {
      guest->hostSide.poll(now);
      for (const coop::Message& message : guest->hostSide.takeWorldMessages()) {
        host.receive(guest->id, message, now);
      }
    }
    field.setInput(stepOf(0.4F));
    const auto outgoing = host.tick(field, 1, std::nullopt, now);
    field.tick(TickSeconds);
    for (const CoopOutgoing& message : outgoing) {
      for (auto& guest : guests) {
        if (guest->id == message.to) {
          (void)guest->hostSide.send(message.message);
        }
      }
    }
    if (ticks % 3 == 0) {
      auto snapshot = makeCoopSnapshot(field, field.mapId(), ++worldTick, 0);
      REQUIRE(snapshot);
      for (auto& guest : guests) {
        if (!starved.contains(guest->id)) {
          (void)guest->hostSide.sendHostSnapshot(*snapshot);
        }
      }
    }
    std::vector<CoopSyncPeer> peers;
    for (auto& guest : guests) {
      peers.push_back({.id = guest->id, .bridge = &guest->hostSide});
    }
    for (std::string& line : sync.hostTick(field, worldTick, peers, now, &host.authority())) {
      log.push_back(std::move(line));
      ++events;
    }
  }

  std::size_t events = 0;
};

// The farthest any side holds any player from where the host has it.
float worstGap(const Session& session) {
  float worst = 0.0F;
  const auto authority = coopSyncAuthority(session.field);
  for (const auto& guest : session.guests) {
    const auto presented = guest->guestSide.presentedWorld(session.now);
    REQUIRE(presented);
    const auto held =
        coopSyncReport(guest->field, &*presented, guest->id, guest->guestSide.snapshotWatch(), 0);
    CHECK(held.players.size() == authority.size());
    for (const coop::SyncHeld& player : held.players) {
      const auto truth = std::ranges::find(authority, player.id, &coop::SyncHeld::id);
      REQUIRE(truth != authority.end());
      CHECK(player.mapId == truth->mapId);
      const auto dx = static_cast<float>(player.x - truth->x);
      const auto dz = static_cast<float>(player.z - truth->z);
      worst = std::max(worst, std::sqrt((dx * dx) + (dz * dz)));
    }
  }
  return worst;
}

void runConverging(std::size_t players) {
  CAPTURE(players);
  Session session{
      players,
      {.latency = Millis{30}, .jitter = Millis{40}, .lossPercent = 10, .duplicatePercent = 2}};
  const glm::vec3 start = session.guests.front()->field.player().position;
  for (int step = 0; step < 240; ++step) {
    session.tick(true);
  }
  // Everyone walked.
  CHECK(glm::distance(session.guests.front()->field.player().position, start) > 100.0F);
  for (int step = 0; step < 300; ++step) {
    session.tick(false);
  }
  // Everyone stands: every side holds every player where the host has it.
  CHECK(worstGap(session) < 2.0F);
  const coop::SyncMonitor* monitor = session.sync.monitor();
  REQUIRE(monitor != nullptr);
  CHECK(monitor->converged(session.now - Millis{1000}));
  CHECK(std::ranges::none_of(monitor->divergences(), &coop::SyncDivergence::persistent));
  // The lossy links lost and reordered snapshots, and the diagnostics said so.
  std::uint32_t lost = 0;
  for (const auto& guest : session.guests) {
    lost += guest->guestSide.snapshotWatch().lost() + guest->guestSide.snapshotWatch().late();
  }
  CHECK(lost > 0);
  CHECK(session.events > 0);
}

bool logged(const Session& session, std::string_view words) {
  return std::ranges::any_of(session.log,
                             [&](const std::string& line) { return line.contains(words); });
}

// The host's snapshots stop reaching the last guest and the host's player is put 1000 units away:
// that guest, and only it, holds the host elsewhere for good until the snapshots come again.
void runStarved(std::size_t players) {
  CAPTURE(players);
  Session session{players, {.latency = Millis{30}, .jitter = Millis{10}}};
  for (int step = 0; step < 120; ++step) {
    session.tick(false);
  }
  const auto cutOff = static_cast<coop::PlayerId>(players - 1);
  session.starved.insert(cutOff);
  session.field.placeAt(session.field.playerEntity(),
                        session.field.player().position + glm::vec3{1000.0F, 0.0F, 0.0F}, 0.0F);
  for (int step = 0; step < 180; ++step) {
    session.tick(false);
  }
  const coop::SyncMonitor* monitor = session.sync.monitor();
  REQUIRE(monitor != nullptr);
  const auto divergences = monitor->divergences();
  REQUIRE(divergences.size() == 1);
  CHECK(divergences.front().guest == cutOff);
  CHECK(divergences.front().player == coop::HostPlayerId);
  CHECK(divergences.front().persistent);
  CHECK(divergences.front().distance == doctest::Approx(1000.0F).epsilon(0.05));
  CHECK(logged(session, std::format("diverged: player {} holds player 0", cutOff)));
  // Every line says when, on the host's clock and world tick.
  CHECK(logged(session, " ms, tick "));
  session.starved.clear();
  for (int step = 0; step < 120; ++step) {
    session.tick(false);
  }
  CHECK(monitor->divergences().empty());
  CHECK(monitor->converged(session.now - Millis{1000}));
  CHECK(logged(session, std::format("in sync: player {} holds player 0", cutOff)));
}

// Four players; one side loads for 30 s without running its loop while the others play on.
void runStalled(coop::PlayerId stalled) {
  CAPTURE(stalled);
  Session session{4, {.latency = Millis{30}, .jitter = Millis{20}, .lossPercent = 5}};
  for (int step = 0; step < 120; ++step) {
    session.tick(true);
  }
  const Millis stallEnd = session.now + Millis{30000};
  Millis quietest{0};
  while (session.now < stallEnd) {
    session.tick(true, stalled);
    if (stalled == 0) {
      for (const auto& guest : session.guests) {
        quietest = std::max(quietest, guest->guestSide.worldQuietFor(session.now));
      }
    }
  }
  // Nobody dropped anyone, and the guests saw the host's world go quiet (their wait notice).
  for (const auto& guest : session.guests) {
    CHECK(guest->hostLink.phase() == coop::LinkPhase::Ready);
    CHECK(guest->guestLink.phase() == coop::LinkPhase::Ready);
  }
  if (stalled == 0) {
    CHECK(quietest > Millis{20000});
  }
  for (int step = 0; step < 300; ++step) {
    session.tick(true);
  }
  for (int step = 0; step < 300; ++step) {
    session.tick(false);
  }
  // Back in step: every side holds every player where the host has it.
  CHECK(worstGap(session) < 2.0F);
  for (const auto& guest : session.guests) {
    CHECK(guest->guestSide.worldQuietFor(session.now) < Millis{1000});
  }
}

} // namespace

TEST_CASE("four players keep their links through a 30 s load on the host or on a guest") {
  runStalled(0);
  runStalled(2);
}

TEST_CASE("with four players every guest's battle messages reach the host on its own link") {
  Session session{4, {}};
  coop::BattleFighterProfile fighter;
  fighter.species = "partner";
  for (const auto& guest : session.guests) {
    REQUIRE(guest->guestSide.sendBattle(coop::BattleAsk{.ask = 7U + guest->id,
                                                        .mapId = "room",
                                                        .enemyNpcs = {1},
                                                        .fixedFight = false,
                                                        .predefined = false,
                                                        .fighter = fighter}));
    REQUIRE(guest->guestSide.sendBattle(coop::BattleOffer{.available = true, .fighter = fighter}));
  }
  for (int step = 0; step < 10; ++step) {
    session.tick(false);
  }
  // The host's loop reads each guest's ask and offer from that guest's bridge (CoopBattles), and
  // answers each on it.
  for (auto& guest : session.guests) {
    CAPTURE(guest->id);
    const auto ask = guest->hostSide.receiveBattle();
    REQUIRE(ask);
    CHECK(std::get<coop::BattleAsk>(*ask).ask == 7U + guest->id);
    const auto offer = guest->hostSide.receiveBattle();
    REQUIRE(offer);
    CHECK(std::get<coop::BattleOffer>(*offer).available);
    REQUIRE(guest->hostSide.sendBattle(
        coop::BattleRefused{.ask = 7U + guest->id, .reason = coop::BattleRefusal::Busy}));
  }
  for (int step = 0; step < 10; ++step) {
    session.tick(false);
  }
  for (auto& guest : session.guests) {
    CAPTURE(guest->id);
    const auto answer = guest->guestSide.receiveBattle();
    REQUIRE(answer);
    CHECK(std::get<coop::BattleRefused>(*answer).ask == 7U + guest->id);
    CHECK(guest->hostLink.phase() == coop::LinkPhase::Ready);
  }
}

TEST_CASE("two players converge over a lossy link (#359)") {
  runConverging(2);
}

TEST_CASE("three players converge over lossy links (#359)") {
  runConverging(3);
}

TEST_CASE("four players converge over lossy links (#359)") {
  runConverging(4);
}

TEST_CASE("two players: a guest cut off from the snapshots shows as a lasting divergence (#359)") {
  runStarved(2);
}

TEST_CASE("three players: only the guest cut off from the snapshots diverges (#359)") {
  runStarved(3);
}

TEST_CASE("four players: only the guest cut off from the snapshots diverges (#359)") {
  runStarved(4);
}

TEST_CASE("traced, the diagnostics log every report next to where the host has each player") {
  Session session{3, {}};
  session.sync.ask("trace");
  for (int step = 0; step < 90; ++step) {
    session.tick(false);
  }
  CHECK(logged(session, "update: player 1 report"));
  CHECK(logged(session, "update: player 2 report"));
  CHECK(logged(session, "holds player 0 at room"));
  const std::size_t traced = session.log.size();
  session.sync.ask("on");
  for (int step = 0; step < 90; ++step) {
    session.tick(false);
  }
  CHECK(session.log.size() == traced);
}

TEST_CASE("a build without the sync diagnostics never watches, and its guests never report") {
  Session session{3, {}};
  // A host built without them: nothing switches them on, nothing goes to the guests.
  session.sync = CoopSync{false};
  session.sync.ask("on");
  for (int step = 0; step < 90; ++step) {
    session.tick(false);
  }
  CHECK_FALSE(session.sync.watching());
  CHECK(session.sync.monitor() == nullptr);
  for (const auto& guest : session.guests) {
    CHECK_FALSE(guest->sync.watched());
  }
  // Guests built without them: the host watches, but hears no report from any.
  session.sync = CoopSync{};
  session.sync.ask("on");
  for (const auto& guest : session.guests) {
    guest->sync = CoopSync{false};
  }
  for (int step = 0; step < 90; ++step) {
    session.tick(false);
  }
  for (const auto& guest : session.guests) {
    CHECK_FALSE(guest->sync.watched());
  }
  REQUIRE(session.sync.monitor() != nullptr);
  CHECK_FALSE(session.sync.monitor()->converged(Millis{0}));
  session.sync.ask("report");
  session.tick(false);
  CHECK(logged(session, "guest 1: no report yet"));
}

TEST_CASE("the sync diagnostics send nothing until switched on, and stop when switched off") {
  Session session{3, {}};
  session.sync.ask("off");
  for (int step = 0; step < 60; ++step) {
    session.tick(false);
  }
  CHECK(session.sync.monitor() == nullptr);
  for (const auto& guest : session.guests) {
    CHECK_FALSE(guest->sync.watched());
  }
  session.sync.ask("on");
  for (int step = 0; step < 90; ++step) {
    session.tick(false);
  }
  for (const auto& guest : session.guests) {
    CHECK(guest->sync.watched());
  }
  REQUIRE(session.sync.monitor() != nullptr);
  CHECK(session.sync.monitor()->converged(session.now - Millis{1000}));
  session.sync.ask("off");
  for (int step = 0; step < 30; ++step) {
    session.tick(false);
  }
  for (const auto& guest : session.guests) {
    CHECK_FALSE(guest->sync.watched());
  }
}
