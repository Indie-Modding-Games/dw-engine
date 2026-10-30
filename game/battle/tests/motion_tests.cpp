#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/flights.hpp>
#include <dwe/game/battle/motion.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fixture.hpp"

using namespace battle_test;

namespace {

// The original's animation ids the battle shows (battle_context.hpp).
constexpr std::int32_t Stance = 0x21;
constexpr std::int32_t WeakStance = 0x22;
constexpr std::int32_t WeakWalk = 0x23;
constexpr std::int32_t Walk = 0x24;
constexpr std::int32_t Block = 0x25;
constexpr std::int32_t Hit = 0x28;
constexpr std::int32_t HitFromBehind = 0x29;
constexpr std::int32_t Faint = 0x2B;
constexpr std::int32_t FirstMove = 0x2E;

// Made-up clips: a walk of 30 units a frame, a hit that steps back `hitStep` a frame for
// `hitFrames` frames, still stances, block and faint, and a move clip of `moveFrames` frames.
struct ClipShape {
  std::size_t hitFrames = 8;
  std::int32_t hitStep = -20;
  std::size_t moveFrames = 20;
};

ClipMotion still(std::size_t frames) {
  return {.steps = std::vector<RootStep>(frames), .repeatFrom = std::nullopt};
}

FighterClips madeUpClips(const ClipShape& shape = {}) {
  const ClipMotion stance{.steps = {RootStep{}}, .repeatFrom = 0};
  const ClipMotion walk{.steps = {RootStep{.side = 0, .forward = 30}}, .repeatFrom = 0};
  const ClipMotion hit{
      .steps = std::vector<RootStep>(shape.hitFrames, {.side = 0, .forward = shape.hitStep}),
      .repeatFrom = std::nullopt};
  return {{Stance, stance},     {WeakStance, stance}, {WeakWalk, walk},
          {Walk, walk},         {Block, still(9)},    {Hit, hit},
          {HitFromBehind, hit}, {Faint, still(21)},   {FirstMove, still(shape.moveFrames)}};
}

BattleSetup clippedDuel(std::vector<std::string> partnerMoves = {"punch"},
                        std::vector<std::string> enemyMoves = {"punch"}) {
  BattleSetup setup = duel(evenStats(400, 120), evenStats(400, 120), partnerMoves, enemyMoves);
  setup.partner.speciesMoves.at(0) = partnerMoves.front();
  setup.enemies.front().speciesMoves.at(0) = enemyMoves.front();
  return setup;
}

Battle clippedBattle(BattleSetup setup, std::uint64_t seed, const ClipShape& shape = {}) {
  Battle battle = startBattle(std::move(setup), seed);
  for (FighterId id = 0; id < battle.state().fighters.size(); ++id) {
    battle.setClips(id, madeUpClips(shape));
  }
  return battle;
}

double length(Point from, Point to) {
  return std::hypot(static_cast<double>(to.x - from.x), static_cast<double>(to.z - from.z));
}

bool happened(const BattleEvents& events, EventKind kind, FighterId fighter) {
  return std::ranges::any_of(events, [&](const BattleEvent& event) {
    return event.kind == kind && event.fighter == fighter;
  });
}

// The frames the first pose `pose` lasts that starts with event `kind` on `fighter`.
std::optional<std::int64_t> firstSpan(Battle& battle, EventKind kind, FighterId fighter,
                                      Pose pose) {
  std::optional<std::int64_t> from;
  while (!battle.finished() && battle.state().frame < 4000) {
    const BattleEvents& events = battle.step();
    if (!from && happened(events, kind, fighter)) {
      from = battle.state().frame;
    }
    if (from && battle.state().fighters[fighter].pose != pose) {
      return battle.state().frame - *from;
    }
  }
  return std::nullopt;
}

// A made-up flight: every move touches its defender on the same attack frame.
class FixedFlights final : public AttackFlights {
public:
  explicit FixedFlights(std::int32_t frame) : frame_(frame) {}

  [[nodiscard]] std::optional<std::int32_t>
  landingFrame(std::string_view move, FighterId attacker, FighterId defender,
               std::optional<std::int32_t> clip) const override {
    asked.push_back({std::string{move}, attacker, defender, clip});
    return frame_;
  }

  struct Question {
    std::string move;
    FighterId attacker = 0;
    FighterId defender = 0;
    std::optional<std::int32_t> clip;
  };
  mutable std::vector<Question> asked;

private:
  std::int32_t frame_;
};

} // namespace

TEST_CASE("a clip's root steps run once, or on from their repeat frame forever") {
  const ClipMotion once{.steps = {{.side = 0, .forward = 1}, {.side = 2, .forward = 3}},
                        .repeatFrom = std::nullopt};
  CHECK(once.length() == 2);
  CHECK(once.stepAt(1) == RootStep{.side = 2, .forward = 3});
  CHECK_FALSE(once.stepAt(2).has_value());
  CHECK_FALSE(once.ended(1));
  CHECK(once.ended(2));

  const ClipMotion loop{
      .steps = {{.side = 0, .forward = 1}, {.side = 0, .forward = 2}, {.side = 0, .forward = 3}},
      .repeatFrom = 1};
  CHECK(loop.stepAt(3) == RootStep{.side = 0, .forward = 2});
  CHECK(loop.stepAt(4) == RootStep{.side = 0, .forward = 3});
  CHECK(loop.stepAt(5) == RootStep{.side = 0, .forward = 2});
  CHECK_FALSE(loop.ended(100));
}

TEST_CASE("a fighter with clips walks by its walk clip and a hit pushes it back by its hit clip") {
  std::int32_t walked = 0;
  std::int32_t pushed = 0;
  for (const std::uint64_t seed : {1U, 2U, 3U}) {
    Battle battle = clippedBattle(clippedDuel(), seed);
    std::vector<Point> before{battle.state().fighters[0].position,
                              battle.state().fighters[1].position};
    while (!battle.finished() && battle.state().frame < 3000) {
      battle.step();
      for (FighterId id = 0; id < before.size(); ++id) {
        const Fighter& fighter = battle.state().fighters[id];
        const double moved = length(before[id], fighter.position);
        if (moved > 0.0) {
          // anim.c: applyRootMomentum: the clip's step along the facing, no other motion.
          if (fighter.shown.clip == Walk || fighter.shown.clip == WeakWalk) {
            CHECK(moved == doctest::Approx(30.0).epsilon(0.05));
            ++walked;
          } else if (fighter.shown.clip == Hit) {
            CHECK(moved == doctest::Approx(20.0).epsilon(0.08));
            const std::int64_t along =
                (static_cast<std::int64_t>(fighter.position.x - before[id].x) * fighter.facing.x) +
                (static_cast<std::int64_t>(fighter.position.z - before[id].z) * fighter.facing.z);
            CHECK(along < 0);
            ++pushed;
          } else {
            CHECK(fighter.shown.clip == HitFromBehind);
          }
        }
        before[id] = fighter.position;
      }
    }
  }
  CHECK(walked > 0);
  CHECK(pushed > 0);
}

TEST_CASE("a hit's step back stops short of the player, as entityCheckCollision tests it") {
  const TimingRules& timing = originalRules().timing;
  std::int32_t held = 0;
  for (const std::uint64_t seed : {1U, 2U, 3U, 4U}) {
    BattleSetup setup = clippedDuel();
    setup.partner.position = {.x = 0, .z = 0};
    setup.enemies.front().position = {.x = 0, .z = 300};
    setup.tamer = {.x = 0, .z = -350};
    // A long push back that would carry the partner far past the player.
    Battle battle = clippedBattle(std::move(setup), seed, {.hitFrames = 40, .hitStep = -25});
    const Point player = battle.state().tamer;
    const std::int64_t reach =
        battle.state().fighters[PartnerId].combatant.radius + timing.retreatRadius;
    const auto apart = [&](Point at) {
      return std::abs(at.x - player.x) > reach || std::abs(at.z - player.z) > reach;
    };
    Point before = battle.state().fighters[PartnerId].position;
    while (!battle.finished() && battle.state().frame < 3000) {
      battle.step();
      const Fighter& partner = battle.state().fighters[PartnerId];
      // A walk goes through the player; a hit's step never takes the partner's square onto the
      // player's (edges touching count).
      if (partner.shown.clip == Hit && apart(before)) {
        CHECK(apart(partner.position));
        held += partner.rootHeld ? 1 : 0;
      }
      before = partner.position;
    }
  }
  CHECK(held > 0);
}

TEST_CASE("a hit reaction lasts as long as its clip plays") {
  const auto span = [](std::size_t frames) {
    Battle battle = clippedBattle(clippedDuel(), 2, {.hitFrames = frames});
    const std::optional<std::int64_t> out =
        firstSpan(battle, EventKind::AttackHit, 1, Pose::HitReaction);
    REQUIRE(out.has_value());
    return *out;
  };
  CHECK(span(30) - span(10) == 20);
}

TEST_CASE("an attack lasts as long as its move's clip plays") {
  const auto span = [](std::size_t frames) {
    Battle battle = clippedBattle(clippedDuel(), 2, {.moveFrames = frames});
    const std::optional<std::int64_t> out =
        firstSpan(battle, EventKind::AttackStarted, 0, Pose::Attacking);
    REQUIRE(out.has_value());
    return *out;
  };
  CHECK(span(40) - span(25) == 15);
}

TEST_CASE("a shot flies until the move's effect touches its target") {
  for (const std::int32_t landing : {20, 33}) {
    FixedFlights flights{landing};
    BattleSetup setup = clippedDuel({"shot"}, {"shot"});
    setup.enemies.front().position = {.x = 0, .z = 650};
    Battle battle = clippedBattle(std::move(setup), 3);
    battle.setFlights(&flights);
    std::optional<AttackInFlight> first;
    std::int32_t release = 0;
    while (!first && !battle.finished() && battle.state().frame < 3000) {
      battle.step();
      for (const AttackInFlight& flying : battle.state().inFlight) {
        first = flying;
        release = battle.state().fighters[flying.attacker].attack->releaseFrame;
      }
    }
    REQUIRE(first.has_value());
    // battle_effect.c: the hit lands on the effect's frame, not after the distance at one speed
    // (the flight already ticked once on the frame it left).
    CHECK(first->framesLeft == landing - release - 1);
    REQUIRE_FALSE(flights.asked.empty());
    CHECK(flights.asked.front().move == "shot");
    CHECK(flights.asked.front().attacker == first->attacker);
    CHECK(flights.asked.front().defender == first->defender);
    CHECK(flights.asked.front().clip == std::optional{FirstMove});
  }
}

namespace {

// The frames of the first attack any fighter starts: as it starts, as it leaves, as its effect
// touches the defender and as the hit or the block lands (each the battle's frame after the step).
struct AttackTimes {
  FighterId attacker = 0;
  std::int64_t started = 0;
  std::optional<std::int64_t> released;
  std::optional<std::int64_t> touched;
  std::optional<std::int64_t> landed;
};

std::optional<AttackTimes> firstAttackTimes(Battle& battle) {
  std::optional<AttackTimes> times;
  while (!battle.finished() && battle.state().frame < 3000) {
    const BattleEvents events = battle.step();
    const std::int64_t frame = battle.state().frame;
    for (const BattleEvent& event : events) {
      if (!times) {
        if (event.kind == EventKind::AttackStarted) {
          times = AttackTimes{.attacker = event.fighter,
                              .started = frame,
                              .released = std::nullopt,
                              .touched = std::nullopt,
                              .landed = std::nullopt};
        }
        continue;
      }
      const bool fromIt = event.other == times->attacker;
      if (event.kind == EventKind::AttackReleased && event.fighter == times->attacker &&
          !times->released) {
        times->released = frame;
      } else if (event.kind == EventKind::AttackLanded && fromIt && !times->touched) {
        times->touched = frame;
      } else if ((event.kind == EventKind::AttackHit || event.kind == EventKind::AttackBlocked) &&
                 fromIt && times->touched) {
        times->landed = frame;
        return times;
      }
    }
  }
  return times;
}

// A made-up flight whose defender moves: the first answer, then another from the next question on.
class ShiftingFlights final : public AttackFlights {
public:
  ShiftingFlights(std::int32_t first, std::int32_t later) : first_(first), later_(later) {}

  [[nodiscard]] std::optional<std::int32_t>
  landingFrame(std::string_view /*move*/, FighterId /*attacker*/, FighterId /*defender*/,
               std::optional<std::int32_t> /*clip*/) const override {
    return asked_++ == 0 ? first_ : later_;
  }

private:
  std::int32_t first_;
  std::int32_t later_;
  mutable std::int32_t asked_ = 0;
};

BattleSetup farShots() {
  BattleSetup setup = clippedDuel({"shot"}, {"shot"});
  setup.enemies.front().position = {.x = 0, .z = 650};
  return setup;
}

} // namespace

TEST_CASE("an effect's attack leaves as it starts and lands the frame after the effect touches") {
  for (const std::int32_t landing : {10, 17}) {
    FixedFlights flights{landing};
    Battle battle = clippedBattle(farShots(), 3);
    battle.setFlights(&flights);
    const std::optional<AttackTimes> times = firstAttackTimes(battle);
    REQUIRE(times.has_value());
    // BTL_startQueuedMove starts the effect with the attack, whatever the ruleset's hit frame.
    CHECK(times->released == std::optional{times->started});
    // The effect's hit check touches on attack frame `landing` (addAttackObject) and
    // BTL_tickAttackHits takes the hit on the next frame.
    REQUIRE(times->touched.has_value());
    CHECK(*times->touched - times->started == landing);
    REQUIRE(times->landed.has_value());
    CHECK(*times->landed - *times->touched == 1);
  }
}

TEST_CASE("an effect's attack follows its defender: asked again each frame, it touches when it "
          "reaches where the defender is now") {
  ShiftingFlights flights{25, 14};
  Battle battle = clippedBattle(farShots(), 3);
  battle.setFlights(&flights);
  const std::optional<AttackTimes> times = firstAttackTimes(battle);
  REQUIRE(times.has_value());
  REQUIRE(times->touched.has_value());
  CHECK(*times->touched - times->started == 14);
}

TEST_CASE("a melee move lands from its effect: it leaves as it starts and lands the frame after "
          "the effect touches") {
  // battle_main.c: BTL_tickMeleeAttack only starts the move in contact; the hit is the effect's
  // (Sonic Jab's apply_line_hit touches on its frame 8, wherever the defender walked to).
  for (const std::int32_t landing : {3, 8}) {
    FixedFlights flights{landing};
    Battle battle = clippedBattle(clippedDuel(), 3);
    battle.setFlights(&flights);
    const std::optional<AttackTimes> times = firstAttackTimes(battle);
    REQUIRE(times.has_value());
    CHECK(times->released == std::optional{times->started});
    REQUIRE(times->touched.has_value());
    CHECK(*times->touched - times->started == landing);
    REQUIRE(times->landed.has_value());
    CHECK(*times->landed - *times->touched == 1);
    REQUIRE_FALSE(flights.asked.empty());
    CHECK(flights.asked.front().move == "punch");
  }
}
