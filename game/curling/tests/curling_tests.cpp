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
  // KAR_D_8005B450: the map objects of each ring.
  CHECK(rules.rings.at(0).objects == std::vector<std::int32_t>{25});
  CHECK(rules.rings.at(1).objects == std::vector<std::int32_t>{19, 20, 21, 22});
  CHECK(rules.rings.at(3).objects == std::vector<std::int32_t>{13, 14, 15});
  CHECK(rules.perfectScore == 10);
}

TEST_CASE("updateRingMarkers: sliding stones light the first free ring they are over") {
  const CurlingRules rules = originalRules();
  Random random{7};
  Match match{rules, Opponent::Penguinmon, random};
  auto teams = emptyRink();
  // Two stones sliding over the centre: the first lights it, the second the ring around it; a
  // resting stone on the left target lights nothing.
  teams.at(PlayerTeam).stones.at(0) = sliding({.x = 0, .z = -1718}, 300, Up);
  teams.at(OpponentTeam).stones.at(0) = sliding({.x = 20, .z = -1700}, 300, Up);
  teams.at(OpponentTeam).stones.at(1) = sliding({.x = -400, .z = -2318}, 0, 0);
  match.setScene(teams, Phase::Rolling, PlayerTeam, 0);
  CHECK(match.litRings() == 0);
  match.tick({}, random);
  CHECK(match.litRings() == 0b0011);

  // Out of the rolling and the tally, every ring is dark.
  match.setScene(teams, Phase::Select, PlayerTeam, 1);
  match.tick({}, random);
  CHECK(match.litRings() == 0);
}

TEST_CASE("updateRingMarkers: the tally lights the ring a stone newly scores in as it blinks") {
  const CurlingRules rules = originalRules();
  Random random{7};
  Match match{rules, Opponent::Penguinmon, random};
  auto teams = emptyRink();
  teams.at(PlayerTeam).stones.at(0) = sliding({.x = -400, .z = -2318}, 0, 0);
  for (std::size_t index = 1; index < StonesPerTeam; ++index) {
    teams.at(PlayerTeam).stones.at(index).state = Waiting;
    teams.at(OpponentTeam).stones.at(index).state = Waiting;
  }
  teams.at(OpponentTeam).stones.at(0).state = Waiting;
  match.setScene(teams, Phase::Rings, PlayerTeam, 0);
  std::int32_t lit = 0;
  std::int32_t dark = 0;
  for (std::int32_t frame = 0; frame < FrameCap && match.phase() != Phase::NextThrower; ++frame) {
    match.tick({}, random);
    if (match.phase() != Phase::Tally) {
      continue;
    }
    CHECK((match.litRings() & ~0b1000U) == 0);
    (match.litRings() != 0 ? lit : dark) += 1;
  }
  CHECK(lit > 0);
  CHECK(dark > 0);
  CHECK(match.litRings() == 0);
}

TEST_CASE("handleAimScroll: Up and Down scroll while the player aims, Left and Right once fast") {
  const CurlingRules rules = originalRules();
  Random random{7};
  Match match{rules, Opponent::Penguinmon, random};
  auto teams = emptyRink();
  teams.at(PlayerTeam).stones.at(0).state = 0;
  match.setScene(teams, Phase::Aim, PlayerTeam, 0);

  CurlingButtons up;
  up.up = true;
  auto events = match.tick(up, random);
  CHECK(Heard{{events.begin(), events.end()}}.has(CurlingEvent::Kind::Scroll, 0, -10));

  CurlingButtons left;
  left.left = true;
  for (std::int32_t frame = 1; frame < 10; ++frame) {
    events = match.tick(left, random);
    CHECK(std::ranges::none_of(events, [](const CurlingEvent& event) {
      return event.kind == CurlingEvent::Kind::Scroll;
    }));
  }
  events = match.tick(left, random);
  CHECK(Heard{{events.begin(), events.end()}}.has(CurlingEvent::Kind::Scroll, -8, 0));

  // The opponent's aim scrolls nothing.
  teams.at(OpponentTeam).stones.at(0).state = 0;
  match.setScene(teams, Phase::Aim, OpponentTeam, 0);
  events = match.tick(up, random);
  CHECK(std::ranges::none_of(
      events, [](const CurlingEvent& event) { return event.kind == CurlingEvent::Kind::Scroll; }));
}

TEST_CASE("the camera follows the thrown stone by its step each frame until it stops") {
  // KAR_tickMatchState state 0xB: moveCameraByDiff from the stone's place on the frame before.
  const CurlingRules rules = originalRules();
  Random random{7};
  Match match{rules, Opponent::Penguinmon, random};
  auto teams = emptyRink();
  Stone& held = teams.at(PlayerTeam).stones.at(0);
  held.state = 0;
  held.angle = Up;
  held.position = PlayerMark;
  match.setScene(teams, Phase::Release, PlayerTeam, 0);
  // The throw's last frame keeps where the stone is before its first slide.
  Point start = PlayerMark;
  for (std::int32_t frame = 0; frame < FrameCap && match.phase() != Phase::Rolling; ++frame) {
    start = match.teams().at(PlayerTeam).stones.at(0).position;
    const auto events = match.tick({}, random);
    CHECK(std::ranges::none_of(events, [](const CurlingEvent& event) {
      return event.kind == CurlingEvent::Kind::Follow;
    }));
  }
  REQUIRE(match.phase() == Phase::Rolling);

  Point followed = start;
  std::int32_t steps = 0;
  for (std::int32_t frame = 0; frame < FrameCap && match.phase() == Phase::Rolling; ++frame) {
    // The state runs before the stones move: each step is the one of the frame before.
    const Point before = match.teams().at(PlayerTeam).stones.at(0).position;
    for (const CurlingEvent& event : match.tick({}, random)) {
      if (event.kind != CurlingEvent::Kind::Follow) {
        continue;
      }
      CHECK(event.a == before.x - followed.x);
      CHECK(event.b == before.z - followed.z);
      followed = before;
      ++steps;
    }
  }
  REQUIRE(match.phase() != Phase::Rolling);
  const Point end = match.teams().at(PlayerTeam).stones.at(0).position;
  CHECK(steps > 10);
  CHECK(followed.x == end.x);
  CHECK(followed.z == end.z);
  // The weakest throw (no power) still slides well up the ice.
  CHECK(end.z < start.z - 500);
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

TEST_CASE("ratan2: the table's steps at the smaller side over the larger one, rounded down") {
  using dwe::core::original::ratan2;
  // Read from the original's table: steps 2, 7 and 768 are 2, 5 and 420.
  CHECK(ratan2(2, 1024) == 2);
  CHECK(ratan2(7, 1024) == 5);
  CHECK(ratan2(7, -1024) == 2048 - 5);
  CHECK(ratan2(-7, -1024) == -(2048 - 5));
  CHECK(ratan2(1024, 7) == 1024 - 5);
  CHECK(ratan2(10, 1030) == 6);           // 10240 / 1030 rounds down to step 9
  CHECK(ratan2(3 << 21, 1 << 23) == 420); // past 2^21 the larger side is cut to 1024ths first
  CHECK(ratan2(1024, 1024) == 512);
  CHECK(ratan2(-5, 0) == -1024);
  CHECK(ratan2(0, -5) == 2048);
  CHECK(ratan2(0, 0) == 0);
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
    // The throwers and the partner turn to the camera in their pose.
    for (std::size_t who = 0; who < 3; ++who) {
      CHECK(match.facing(who) == 0x800);
      CHECK(heard.has(CurlingEvent::Kind::Animation, static_cast<std::int32_t>(who), 2));
    }
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

TEST_CASE("stoneModelObject: KARRING's object by team and type, spiked stones plain in hand") {
  // KAR_setupMatch: type * 3 + KAR_D_8005AB80 {2, 1, 0}; KAR_beginAiming links a spiked stone to
  // its team's type 0 until KAR_tickStones pegs it (MAIN_D_80134A4C {11, 10, 9}).
  const auto object = [](std::size_t team, std::int32_t type, std::int32_t state) {
    Stone stone;
    stone.type = type;
    stone.state = state;
    return stoneModelObject(team, stone);
  };
  CHECK(object(PlayerTeam, 0, Waiting) == 2);
  CHECK(object(OpponentTeam, 0, Waiting) == 1);
  CHECK(object(ObstacleTeam, 0, Sliding) == 0);
  CHECK(object(OpponentTeam, 2, Waiting) == 7);
  CHECK(object(PlayerTeam, 1, Sliding) == 5);
  CHECK(object(PlayerTeam, 3, Waiting) == 11);
  CHECK(object(PlayerTeam, 3, 0) == 2);
  CHECK(object(OpponentTeam, 3, Sliding) == 1);
  CHECK(object(PlayerTeam, 3, 2) == 11);
  CHECK(object(OpponentTeam, 3, 3) == 10);
  CHECK(object(ObstacleTeam, 3, Sliding) == 9);
  for (std::size_t team = 0; team < Teams; ++team) {
    for (std::int32_t type = 0; type < 4; ++type) {
      CHECK(object(team, type, Waiting) < StoneModelObjects);
    }
  }
}

TEST_CASE("stoneDrawn: the racks fill one stone each ten frames after the start, then all show") {
  // KAR_renderScene: before the demonstration throw a stone i shows once the intro counter
  // (6 a frame in state 3) reaches i * 60 + 60; the opponent's rack shows while "Ready" blinks.
  const CurlingRules rules = originalRules();
  Random random{7};
  Match match{rules, Opponent::Penguinmon, random};
  std::array<std::int32_t, StonesPerTeam> firstShown{};
  firstShown.fill(-1);
  std::int32_t readyFrames = 0;
  for (std::int32_t frame = 0; frame < 400 && match.phase() != Phase::Select; ++frame) {
    CurlingButtons buttons;
    buttons.confirmPressed = match.phase() == Phase::Prompt && match.promptOption().has_value();
    match.tick(buttons, random);
    if (match.phase() == Phase::Prompt || match.phase() == Phase::Placing) {
      for (std::size_t team = 0; team < Teams; ++team) {
        for (std::size_t index = 0; index < StonesPerTeam; ++index) {
          CHECK_FALSE(match.stoneDrawn(team, index));
        }
      }
    }
    if (match.phase() != Phase::Ready) {
      continue;
    }
    ++readyFrames;
    for (std::size_t index = 0; index < StonesPerTeam; ++index) {
      if (firstShown.at(index) < 0 && match.stoneDrawn(PlayerTeam, index)) {
        firstShown.at(index) = readyFrames;
        CHECK(match.stoneDrawn(OpponentTeam, index));
      }
    }
  }
  REQUIRE(match.phase() == Phase::Select);
  for (std::size_t index = 1; index < StonesPerTeam; ++index) {
    CAPTURE(index);
    REQUIRE(firstShown.at(index - 1) > 0);
    CHECK(firstShown.at(index) - firstShown.at(index - 1) == 10);
  }
  // Picking a stone: both racks show; a stone out of play does not.
  for (std::size_t index = 0; index < StonesPerTeam; ++index) {
    CHECK(match.stoneDrawn(PlayerTeam, index));
    CHECK(match.stoneDrawn(OpponentTeam, index));
  }
  auto teams = match.teams();
  teams.at(ObstacleTeam).stones.at(4).state = Out;
  match.setScene(teams, Phase::Select, PlayerTeam, 0);
  CHECK_FALSE(match.stoneDrawn(ObstacleTeam, 4));
}

TEST_CASE("facing: the thrower faces 0 to aim and turns against the aim, the stone slides so") {
  // KAR_beginAiming: rotation 0 and the aim at -0x400; KAR_turnAimLeft / KAR_turnAimRight turn
  // the thrower by -delta; state 8 gives the stone the thrower's facing (KAR_renderScene clears it
  // in the player's hand and the second opponent's, not beside the first).
  const CurlingRules rules = originalRules();
  Random random{7};
  Match match{rules, Opponent::Penguinmon, random};
  CHECK_FALSE(match.facing(0).has_value());
  Heard heard;
  for (std::int32_t frame = 0; frame < 400 && match.phase() != Phase::Select; ++frame) {
    CurlingButtons buttons;
    buttons.confirmPressed = match.phase() == Phase::Prompt && match.promptOption().has_value();
    const auto events = match.tick(buttons, random);
    heard.events.insert(heard.events.end(), events.begin(), events.end());
  }
  REQUIRE(match.phase() == Phase::Select);
  CHECK(match.facing(0) == 0);
  CHECK(match.facing(1) == 0);
  CHECK_FALSE(match.facing(2).has_value());

  CurlingButtons confirm;
  confirm.confirmPressed = true;
  auto events = match.tick(confirm, random);
  REQUIRE(match.phase() == Phase::Aim);
  CHECK(Heard{{events.begin(), events.end()}}.has(CurlingEvent::Kind::Animation, 0, 0x23));
  CHECK(match.facing(0) == 0);
  CHECK(match.aimAngle() == -0x400);
  CurlingButtons left;
  left.left = true;
  for (std::int32_t frame = 0; frame < 5; ++frame) {
    match.tick(left, random);
  }
  CHECK(match.aimAngle() < -0x400);
  CHECK(match.facing(0) == -0x400 - match.aimAngle());

  // The first opponent's throw: its stone keeps the facing it let go with.
  auto teams = emptyRink();
  teams.at(OpponentTeam).stones.at(0).state = 0;
  teams.at(OpponentTeam).stones.at(0).angle = -0x500;
  teams.at(OpponentTeam).plannedPower = 1000;
  match.setScene(teams, Phase::Aim, OpponentTeam, 0);
  for (std::int32_t frame = 0; frame < FrameCap && match.phase() != Phase::Rolling; ++frame) {
    match.tick({}, random);
  }
  REQUIRE(match.phase() == Phase::Rolling);
  REQUIRE(match.facing(1).has_value());
  CHECK(*match.facing(1) != 0);
  CHECK(match.teams().at(OpponentTeam).stones.at(0).spin == *match.facing(1));
}
