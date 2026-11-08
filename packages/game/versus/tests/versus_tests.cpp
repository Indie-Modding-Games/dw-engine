#include <dwe/game/versus/link.hpp>
#include <dwe/game/versus/protocol.hpp>
#include <dwe/game/versus/series.hpp>
#include <dwe/net/loopback.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace dwe::game::versus;
using dwe::net::LinkConditions;
using dwe::net::LoopbackNetwork;
using dwe::net::Millis;

namespace {

constexpr Millis Frame{50};

Identity identity(std::string version = "0.1.0", std::string content = "abc") {
  return {.protocol = ProtocolVersion,
          .gameVersion = std::move(version),
          .contentHash = std::move(content)};
}

VersusFighter fighter(std::string name, std::int32_t brains) {
  return {.name = std::move(name),
          .species = "species_a",
          .hp = 1500,
          .mp = 800,
          .offense = 200,
          .defense = 150,
          .speed = 180,
          .brains = brains,
          .discipline = 80,
          .techniques = {std::string{"move_a"}, std::nullopt, std::string{"move_b"}}};
}

SeriesSetup threeBattles() {
  return {.battles = 3, .arena = 1, .seed = 77, .inputDelay = 3};
}

struct Pair {
  explicit Pair(LinkConditions conditions = {}, Identity guestIdentity = identity())
      : network{conditions, 5}, host{VersusLink::host(network.peer(0), identity(), threeBattles())},
        guest{VersusLink::guest(network.peer(1), std::move(guestIdentity))} {}

  void run(int frames) {
    for (int frame = 0; frame < frames; ++frame) {
      host.poll(network.now());
      guest.poll(network.now());
      network.advance(Frame);
    }
  }

  LoopbackNetwork network;
  VersusLink host;
  VersusLink guest;
};

// A toy battle: it ends after `length` ticks and its result comes from the inputs.
struct ToyBattle {
  std::uint64_t state = 1;
  std::uint32_t ticks = 0;
  void step(std::array<std::uint32_t, 2> inputs) {
    state = (state * 1103515245ULL) + inputs[0] + (3ULL * inputs[1]) + 7;
    ++ticks;
  }
  [[nodiscard]] RoundResult result() const {
    return state % 2 == 0 ? RoundResult::HostWon : RoundResult::GuestWon;
  }
};

// One frame of a side in a battle: input, network, the ticks it can play, and the end.
void playFrame(VersusLink& link, ToyBattle& battle, std::uint32_t input, std::uint32_t length,
               bool lie = false) {
  if (link.awaitingResult() || link.lockstep() == nullptr) {
    return;
  }
  link.submitInput(input);
  while (link.lockstep() != nullptr && !link.awaitingResult()) {
    const auto inputs = link.lockstep()->ready();
    if (!inputs) {
      break;
    }
    battle.step(*inputs);
    link.playedTick(battle.state);
    if (battle.ticks == length) {
      const RoundResult result = lie ? RoundResult::BothLost : battle.result();
      link.finishRound(result, battle.state);
    }
  }
}

} // namespace

TEST_CASE("the versus messages read back as they were written") {
  const std::vector<Message> messages = {
      Hello{.identity = identity()},
      Welcome{.identity = identity("0.2.0", "def"), .setup = threeBattles()},
      Lineup{.fighters = {fighter("first", 300), fighter("second", 600)}},
      RoundOver{.round = 2, .result = -1, .checksum = 0xDEADBEEFULL},
      Leave{.reason = LeaveReason::ContentMismatch},
  };
  for (const Message& message : messages) {
    const auto decoded = decode(encode(message));
    REQUIRE(decoded);
    REQUIRE(*decoded);
    CHECK(encode(**decoded) == encode(message));
  }
  const auto lineup = decode(encode(messages[2]));
  CHECK(std::get<Lineup>(**lineup).fighters.at(1) == fighter("second", 600));
  // Another protocol's message is not one of these; a cut one is damaged.
  const auto other = decode(dwe::net::Bytes{0x40, 1, 2});
  REQUIRE(other);
  CHECK_FALSE(*other);
  dwe::net::Bytes cut = encode(messages[1]);
  cut.pop_back();
  CHECK_FALSE(decode(cut));
}

TEST_CASE("vs_scene.c VS_initializeVS: every battle of the series is played, more wins win") {
  Series series{3};
  series.record(RoundResult::HostWon);
  CHECK_FALSE(series.finished());
  CHECK_FALSE(series.winner());
  series.record(RoundResult::BothLost);
  series.record(RoundResult::GuestWon);
  CHECK(series.finished());
  CHECK(series.wins(Side::Host) == 1);
  CHECK(series.losses(Side::Host) == 2);
  CHECK(series.wins(Side::Guest) == 1);
  CHECK(series.wonRound(Side::Host, 0));
  CHECK_FALSE(series.wonRound(Side::Guest, 1));
  CHECK_FALSE(series.winner());
  Series single{1};
  single.record(RoundResult::GuestWon);
  CHECK(single.winner() == Side::Guest);
}

TEST_CASE("the guest is welcomed with the series and both send their lineups") {
  Pair pair{{.latency = Millis{40}, .jitter = Millis{40}, .lossPercent = 20}};
  pair.run(40);
  REQUIRE(pair.host.phase() == LinkPhase::Lineups);
  REQUIRE(pair.guest.phase() == LinkPhase::Lineups);
  CHECK(pair.guest.setup() == threeBattles());
  CHECK_FALSE(pair.host.sendLineup({fighter("one", 100)}));
  REQUIRE(pair.host.sendLineup({fighter("a", 100), fighter("b", 200), fighter("c", 300)}));
  REQUIRE(pair.guest.sendLineup({fighter("x", 100), fighter("y", 200), fighter("z", 300)}));
  pair.run(40);
  CHECK(pair.host.phase() == LinkPhase::Playing);
  CHECK(pair.guest.phase() == LinkPhase::Playing);
  REQUIRE(pair.host.lineup(Side::Guest));
  CHECK(pair.host.lineup(Side::Guest)->at(2).name == "z");
}

TEST_CASE("a guest with another game version or other content is refused, with the reason") {
  Pair version{{}, identity("0.9.0")};
  version.run(20);
  CHECK(version.host.failure() == LinkFailure::VersionMismatch);
  CHECK(version.guest.failure() == LinkFailure::VersionMismatch);
  Pair content{{}, identity("0.1.0", "other")};
  content.run(20);
  CHECK(content.host.failure() == LinkFailure::ContentMismatch);
  CHECK(content.guest.failure() == LinkFailure::ContentMismatch);
}

TEST_CASE("a whole series in lockstep over a bad link, round by round") {
  Pair pair{{.latency = Millis{30}, .jitter = Millis{50}, .lossPercent = 15}};
  pair.run(30);
  REQUIRE(pair.host.sendLineup({fighter("a", 100), fighter("b", 200), fighter("c", 300)}));
  REQUIRE(pair.guest.sendLineup({fighter("x", 100), fighter("y", 200), fighter("z", 300)}));
  pair.run(30);
  REQUIRE(pair.host.phase() == LinkPhase::Playing);
  for (std::int32_t round = 0; round < 3; ++round) {
    CAPTURE(round);
    ToyBattle hostBattle;
    ToyBattle guestBattle;
    REQUIRE(pair.host.startRound());
    // The guest starts a little later, as after a longer scoreboard.
    pair.run(5);
    REQUIRE(pair.guest.startRound());
    for (int frame = 0; frame < 400 && pair.host.series().round() == round; ++frame) {
      const auto length = static_cast<std::uint32_t>(120 + (round * 10));
      playFrame(pair.host, hostBattle, static_cast<std::uint32_t>(frame % 4), length);
      playFrame(pair.guest, guestBattle, static_cast<std::uint32_t>(frame % 3), length);
      pair.run(1);
    }
    pair.run(20);
    CHECK(pair.host.series().round() == round + 1);
    CHECK(pair.guest.series().round() == round + 1);
    CHECK(hostBattle.state == guestBattle.state);
  }
  CHECK(pair.host.phase() == LinkPhase::Finished);
  CHECK(pair.guest.phase() == LinkPhase::Finished);
  CHECK(pair.host.series().results() == pair.guest.series().results());
}

TEST_CASE("results that disagree end the series as a desync on both sides") {
  Pair pair;
  pair.run(10);
  REQUIRE(pair.host.sendLineup({fighter("a", 100), fighter("b", 200), fighter("c", 300)}));
  REQUIRE(pair.guest.sendLineup({fighter("x", 100), fighter("y", 200), fighter("z", 300)}));
  pair.run(10);
  ToyBattle hostBattle;
  ToyBattle guestBattle;
  REQUIRE(pair.host.startRound());
  REQUIRE(pair.guest.startRound());
  for (int frame = 0; frame < 200; ++frame) {
    playFrame(pair.host, hostBattle, 1, 50);
    playFrame(pair.guest, guestBattle, 2, 50, true);
    pair.run(1);
  }
  CHECK(pair.host.failure() == LinkFailure::Desync);
  CHECK(pair.guest.failure() == LinkFailure::Desync);
}

TEST_CASE("a guest that hears nothing gives up, and a silent peer ends the series") {
  LoopbackNetwork nobody{{}};
  VersusLink alone = VersusLink::guest(nobody.peer(1), identity());
  for (int frame = 0; frame < 300; ++frame) {
    alone.poll(nobody.now());
    nobody.advance(Frame);
  }
  CHECK(alone.failure() == LinkFailure::NoAnswer);

  Pair pair;
  pair.run(10);
  REQUIRE(pair.host.phase() == LinkPhase::Lineups);
  pair.network.cut();
  pair.run(200);
  CHECK(pair.host.failure() == LinkFailure::Silence);
  CHECK(pair.guest.failure() == LinkFailure::Silence);
}

TEST_CASE("a side that leaves tells the other one") {
  Pair pair;
  pair.run(10);
  pair.guest.leave();
  pair.run(10);
  CHECK(pair.guest.failure() == LinkFailure::Left);
  CHECK(pair.host.failure() == LinkFailure::Left);
}
