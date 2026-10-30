#include <dwe/assets/file_system.hpp>
#include <dwe/core/original_math.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/curling/curling.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace dwe::game::curling;
using dwe::ruleset::Random;

constexpr std::int32_t Waiting = -1;
constexpr std::int32_t Out = -101;
constexpr std::int32_t Sliding = 1;
constexpr std::int32_t Up = -0x400; // toward the target (z down)
constexpr std::int32_t FrameCap = 4000;

CurlingRules originalRules() {
  const auto bytes = dwe::assets::DiskFileSystem{}.readFile(std::filesystem::path{DWE_GAME_PACK} /
                                                            "rules" / "original" / "curling.yaml");
  REQUIRE(bytes);
  auto node = dwe::core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  REQUIRE(node);
  auto rules = parseCurlingRules(
      {.name = "curling", .owner = "test", .file = "curling.yaml", .node = *node});
  REQUIRE_MESSAGE(rules, (rules ? std::string{} : rules.error().message));
  return *rules;
}

// Every stone out of play.
std::array<Team, Teams> emptyRink() {
  std::array<Team, Teams> teams{};
  for (Team& team : teams) {
    for (Stone& stone : team.stones) {
      stone.state = Out;
      stone.weight = 30;
    }
  }
  return teams;
}

Stone sliding(Point at, std::int32_t speed, std::int32_t angle, std::int32_t type = 0,
              std::int32_t weight = 30) {
  Stone stone;
  stone.state = Sliding;
  stone.type = type;
  stone.weight = weight;
  stone.speed = speed;
  stone.angle = angle;
  stone.position = at;
  stone.target = at;
  return stone;
}

struct Heard {
  std::vector<CurlingEvent> events;
  bool has(CurlingEvent::Kind kind, std::int32_t a, std::int32_t b) const {
    return std::ranges::any_of(events, [&](const CurlingEvent& event) {
      return event.kind == kind && event.a == a && event.b == b;
    });
  }
};

// Ticks until the throw has stopped (the match leaves Rolling), keeping the events.
Heard rollToStop(Match& match, Random& random) {
  Heard heard;
  for (std::int32_t frame = 0; frame < FrameCap && match.phase() == Phase::Rolling; ++frame) {
    const auto events = match.tick({}, random);
    heard.events.insert(heard.events.end(), events.begin(), events.end());
  }
  REQUIRE(match.phase() != Phase::Rolling);
  return heard;
}

std::int32_t gap(const Stone& a, const Stone& b) {
  const double dx = a.position.x - b.position.x;
  const double dz = a.position.z - b.position.z;
  return static_cast<std::int32_t>(std::sqrt((dx * dx) + (dz * dz)));
}

} // namespace

TEST_CASE("curling.yaml: the section reads with the original's numbers") {
  const CurlingRules rules = originalRules();
  CHECK(rules.opponentVariable == 122);
  CHECK(rules.resultVariable == 121);
  CHECK(rules.stoneTypes.size() == 10);
  CHECK(rules.weights.at(2) == 35);
  CHECK(rules.frictionKeep == 98);
  CHECK(rules.powerLimit == 3000);
  CHECK(rules.throwBonus == 600);
  CHECK(rules.opponentPowerOffsets.at(1) == 500);
  REQUIRE(rules.rings.size() == 4);
  CHECK(rules.rings.at(0).radius == 145);
  CHECK(rules.rings.at(2).points == -2);
  CHECK(rules.rings.at(3).x == -400);
  CHECK(rules.perfectScore == 10);
}

TEST_CASE("setupMatch: the racks and the obstacles take the original's draws in order") {
  const CurlingRules rules = originalRules();
  constexpr std::uint32_t Seed = 0x1234ABCD;
  Random random = Random::original(Seed);
  const Match match{rules, Opponent::Penguinmon, random};

  Random mirror = Random::original(Seed);
  const auto roll = [&mirror](std::uint32_t limit) {
    return static_cast<std::int32_t>(mirror.next() % limit);
  };
  constexpr std::array<std::int32_t, 3> ObstacleX{0x2C1, 0xEB, -0xEB};
  const std::int32_t obstacles = roll(3);
  for (std::size_t team = 0; team < Teams; ++team) {
    static_cast<void>(mirror.next());
    for (std::size_t index = 0; index < StonesPerTeam; ++index) {
      const Stone& stone = match.teams().at(team).stones.at(index);
      const std::int32_t type = rules.stoneTypes.at(static_cast<std::size_t>(roll(10)));
      CAPTURE(team);
      CAPTURE(index);
      CHECK(stone.type == type);
      CHECK(stone.weight == rules.weights.at(static_cast<std::size_t>(type)));
      CHECK(stone.speed == 0);
      if (team < ObstacleTeam) {
        CHECK(stone.state == Waiting);
        CHECK(stone.position.x == (team == 0 ? -700 : 700));
        CHECK(stone.height == (static_cast<std::int32_t>(index) * 170) - 80);
      } else if (std::cmp_less(index, obstacles)) {
        CHECK(stone.state == Sliding);
        const std::int32_t x = ObstacleX.at(index) - roll(0x13F);
        const std::int32_t z = -(roll(1000) + 1000);
        CHECK(stone.position.x == x);
        CHECK(stone.position.z == z);
      } else {
        CHECK(stone.state == Out);
      }
    }
  }
  CHECK(match.phase() == Phase::Placing);
  CHECK(match.throwingTeam() == 0);
}

TEST_CASE("tickStones: a stone slides speed / weight a frame, keeps 98% and stops under 2") {
  const CurlingRules rules = originalRules();
  Random random{7};
  Match match{rules, Opponent::Penguinmon, random};
  auto teams = emptyRink();
  teams.at(PlayerTeam).stones.at(0) = sliding({.x = 0, .z = 0}, 900, Up);
  match.setScene(teams, Phase::Rolling, PlayerTeam, 0);

  std::int32_t z = 0;
  std::int32_t speed = 900;
  std::int32_t frames = 0;
  while (speed > 0) {
    const std::int32_t move = speed / 30;
    speed = static_cast<std::int32_t>(static_cast<std::uint32_t>(speed * 98) / 100U);
    if (move < 2) {
      speed = 0;
    }
    z += move * dwe::core::original::rsin(Up) / 4096;
    ++frames;
  }
  for (std::int32_t frame = 0; frame < frames; ++frame) {
    REQUIRE(match.phase() == Phase::Rolling);
    match.tick({}, random);
  }
  const Stone& stone = match.teams().at(PlayerTeam).stones.at(0);
  CHECK(stone.speed == 0);
  CHECK(stone.position.x == 0);
  CHECK(stone.position.z == z);
  CHECK(z < -1000);
  // The next frame sees every stone still and goes on to the rings.
  match.tick({}, random);
  CHECK(match.phase() == Phase::Rings);
}

TEST_CASE("tickStones: a stopped stone near the throwers goes out") {
  const CurlingRules rules = originalRules();
  Random random{7};
  Match match{rules, Opponent::Penguinmon, random};
  auto teams = emptyRink();
  teams.at(PlayerTeam).stones.at(0) = sliding({.x = 0, .z = 400}, 120, Up);
  match.setScene(teams, Phase::Rolling, PlayerTeam, 0);
  rollToStop(match, random);
  CHECK(match.teams().at(PlayerTeam).stones.at(0).state == Out);
}

TEST_CASE("tickStones: a stone with spikes pegs itself to the ice when it stops") {
  const CurlingRules rules = originalRules();
  Random random{7};
  Match match{rules, Opponent::Penguinmon, random};
  auto teams = emptyRink();
  teams.at(PlayerTeam).stones.at(0) = sliding({.x = 0, .z = 0}, 600, Up, 3, rules.weights.at(3));
  match.setScene(teams, Phase::Rolling, PlayerTeam, 0);
  Heard heard = rollToStop(match, random);
  const auto events = match.tick({}, random);
  heard.events.insert(heard.events.end(), events.begin(), events.end());
  const Stone& stone = match.teams().at(PlayerTeam).stones.at(0);
  CHECK(stone.pegged());
  CHECK(stone.state == 2);
  CHECK(heard.has(CurlingEvent::Kind::Sound, 8, 4));
}

TEST_CASE("bounceOffWall: a stone off the right wall comes back mirrored") {
  const CurlingRules rules = originalRules();
  Random random{7};
  Match match{rules, Opponent::Penguinmon, random};
  auto teams = emptyRink();
  teams.at(PlayerTeam).stones.at(0) = sliding({.x = 600, .z = -1000}, 600, 0);
  match.setScene(teams, Phase::Rolling, PlayerTeam, 0);
  Heard heard;
  for (std::int32_t frame = 0; frame < FrameCap && match.phase() == Phase::Rolling; ++frame) {
    const auto events = match.tick({}, random);
    heard.events.insert(heard.events.end(), events.begin(), events.end());
    CHECK(match.teams().at(PlayerTeam).stones.at(0).position.x <= 0x2D5);
  }
  const Stone& stone = match.teams().at(PlayerTeam).stones.at(0);
  CHECK(heard.has(CurlingEvent::Kind::Sound, 8, 3));
  CHECK(stone.position.x < 600);
  CHECK(stone.position.z == -1000);
  CHECK(stone.angle % 4096 == 2048);
}

TEST_CASE("updateCollisions: a thrown stone knocks a resting one on and they end apart") {
  const CurlingRules rules = originalRules();
  Random random{7};
  Match match{rules, Opponent::Penguinmon, random};
  auto teams = emptyRink();
  teams.at(PlayerTeam).stones.at(0) = sliding({.x = 0, .z = 0}, 1800, Up);
  teams.at(OpponentTeam).stones.at(0) = sliding({.x = 0, .z = -500}, 0, 0);
  match.setScene(teams, Phase::Rolling, PlayerTeam, 0);
  const Heard heard = rollToStop(match, random);
  const Stone& thrown = match.teams().at(PlayerTeam).stones.at(0);
  const Stone& hit = match.teams().at(OpponentTeam).stones.at(0);
  CHECK(hit.position.z < -500);
  CHECK(thrown.position.z > hit.position.z);
  CHECK(gap(thrown, hit) >= 0x96);
  CHECK((heard.has(CurlingEvent::Kind::Sound, 8, 1) || heard.has(CurlingEvent::Kind::Sound, 8, 2)));
}

TEST_CASE("classifyStoneRings and the tally: each team scores its rings, newest first") {
  const CurlingRules rules = originalRules();
  for (const bool skip : {false, true}) {
    CAPTURE(skip);
    Random random{7};
    Match match{rules, Opponent::Penguinmon, random};
    auto teams = emptyRink();
    teams.at(PlayerTeam).stones.at(0) = sliding({.x = 0, .z = -1718}, 0, 0);
    teams.at(PlayerTeam).stones.at(1) = sliding({.x = 410, .z = -2300}, 0, 0);
    teams.at(OpponentTeam).stones.at(0) = sliding({.x = -400, .z = -2318}, 0, 0);
    teams.at(OpponentTeam).stones.at(1) = sliding({.x = 0, .z = -1300}, 0, 0); // short of the line
    for (std::size_t index = 2; index < StonesPerTeam; ++index) {
      teams.at(PlayerTeam).stones.at(index).state = Waiting;
      teams.at(OpponentTeam).stones.at(index).state = Waiting;
    }
    teams.at(PlayerTeam).thrown = 1;
    teams.at(OpponentTeam).thrown = 2;
    match.setScene(teams, Phase::Rings, PlayerTeam, 1);

    Heard heard;
    std::int32_t frame = 0;
    for (; frame < FrameCap && match.phase() != Phase::NextThrower; ++frame) {
      CurlingButtons buttons;
      buttons.confirmPressed = skip && frame == 3;
      const auto events = match.tick(buttons, random);
      heard.events.insert(heard.events.end(), events.begin(), events.end());
    }
    REQUIRE(match.phase() == Phase::NextThrower);
    const auto& player = match.teams().at(PlayerTeam);
    const auto& rival = match.teams().at(OpponentTeam);
    CHECK(player.stones.at(0).ring == 1);
    CHECK(player.stones.at(1).ring == 3);
    CHECK(rival.stones.at(0).ring == (0x10 | 4));
    CHECK(rival.stones.at(1).ring == 0);
    CHECK(player.thrown == 2);
    CHECK(rival.thrown == 2);
    CHECK(player.score == 2 - 2);
    CHECK(rival.score == 2);
    for (const Team& team : match.teams()) {
      for (const Stone& stone : team.stones) {
        CHECK(stone.state < 100);
      }
    }
    if (!skip) {
      CHECK(heard.has(CurlingEvent::Kind::Sound, 8, 6));
      CHECK(heard.has(CurlingEvent::Kind::Sound, 8, 5));
      CHECK(heard.has(CurlingEvent::Kind::Tally, 1, 4));
      CHECK(frame > 200);
    } else {
      CHECK(frame < 10);
    }
  }
}

TEST_CASE("finishMatch: 2 a win of 10 or more, 1 a win, 0 a draw or a loss") {
  const CurlingRules rules = originalRules();
  struct Case {
    std::int32_t player;
    std::int32_t rival;
    std::int32_t result;
  };
  for (const Case expected :
       {Case{.player = 10, .rival = 3, .result = 2}, Case{.player = 5, .rival = 3, .result = 1},
        Case{.player = 3, .rival = 3, .result = 0}, Case{.player = 3, .rival = 12, .result = 0}}) {
    CAPTURE(expected.player);
    CAPTURE(expected.rival);
    Random random{7};
    Match match{rules, Opponent::MetalMamemon, random};
    auto teams = emptyRink();
    teams.at(PlayerTeam).score = expected.player;
    teams.at(PlayerTeam).thrown = 5;
    teams.at(OpponentTeam).score = expected.rival;
    teams.at(OpponentTeam).thrown = 5;
    match.setScene(teams, Phase::Turn, OpponentTeam, 4);
    Heard heard;
    for (std::int32_t frame = 0; frame < 40 && !match.finished(); ++frame) {
      CurlingButtons buttons;
      buttons.confirmPressed = frame % 2 == 0;
      const auto events = match.tick(buttons, random);
      heard.events.insert(heard.events.end(), events.begin(), events.end());
    }
    REQUIRE(match.finished());
    CHECK(match.result() == expected.result);
    CHECK(heard.has(CurlingEvent::Kind::Finished, expected.result, 0));
    CHECK(heard.has(CurlingEvent::Kind::Sound, 8, expected.result != 0 ? 7 : 8));
  }
}

namespace {

struct Played {
  std::int32_t result = -1;
  std::array<std::int32_t, 2> scores{};
  std::array<Point, StonesPerTeam * 2> stones{};
  std::int32_t frames = 0;
  std::int32_t finishedEvents = 0;
};

// A whole match: the player throws with the meter past 1800 and pages through every box.
Played playMatch(const CurlingRules& rules, Opponent opponent, std::uint64_t seed,
                 bool demonstration) {
  Random random{seed};
  Match match{rules, opponent, random};
  Played played;
  bool askedRules = false;
  for (; played.frames < 60000 && !match.finished(); ++played.frames) {
    CurlingButtons buttons;
    const bool odd = played.frames % 2 != 0;
    if (match.phase() == Phase::Prompt && demonstration && !askedRules && match.promptOption()) {
      buttons.downPressed = true;
      askedRules = true;
    } else if (match.phase() == Phase::Power) {
      buttons.confirmPressed = match.power() >= 1800;
    } else {
      buttons.confirmPressed = odd;
    }
    for (const CurlingEvent& event : match.tick(buttons, random)) {
      if (event.kind == CurlingEvent::Kind::Finished) {
        ++played.finishedEvents;
      }
    }
  }
  played.result = match.finished() ? match.result() : -1;
  played.scores = {match.teams().at(PlayerTeam).score, match.teams().at(OpponentTeam).score};
  for (std::size_t team = 0; team < 2; ++team) {
    for (std::size_t index = 0; index < StonesPerTeam; ++index) {
      played.stones.at((team * StonesPerTeam) + index) =
          match.teams().at(team).stones.at(index).position;
    }
    CHECK(match.teams().at(team).thrown == 5);
  }
  return played;
}

} // namespace

TEST_CASE("a whole match with a fixed seed plays to its end, the same every time") {
  const CurlingRules rules = originalRules();
  for (const Opponent opponent : {Opponent::Penguinmon, Opponent::MetalMamemon}) {
    for (const bool demonstration : {false, true}) {
      CAPTURE(static_cast<int>(opponent));
      CAPTURE(demonstration);
      const Played first = playMatch(rules, opponent, 2024, demonstration);
      REQUIRE(first.result >= 0);
      CHECK(first.finishedEvents == 1);
      const std::int32_t expected =
          first.scores[0] > first.scores[1] ? (first.scores[0] >= rules.perfectScore ? 2 : 1) : 0;
      CHECK(first.result == expected);
      const Played again = playMatch(rules, opponent, 2024, demonstration);
      CHECK(again.result == first.result);
      CHECK(again.scores == first.scores);
      CHECK(again.frames == first.frames);
      for (std::size_t index = 0; index < first.stones.size(); ++index) {
        CHECK(again.stones.at(index).x == first.stones.at(index).x);
        CHECK(again.stones.at(index).z == first.stones.at(index).z);
      }
    }
  }
}
