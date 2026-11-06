#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/flights.hpp>
#include <dwe/game/battle/motion.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <numeric>
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

// A made-up flight: every move touches its defender on the same attack frame, at a fixed point
// or, without one, at the attacker's place.
class FixedFlights final : public AttackFlights {
public:
  explicit FixedFlights(std::int32_t frame, std::optional<Point> at = std::nullopt)
      : frame_(frame), at_(at) {}

  void setBattle(const Battle& battle) { battle_ = &battle; }

  [[nodiscard]] std::optional<EffectLanding>
  landing(std::string_view move, FighterId attacker, FighterId defender,
          std::optional<std::int32_t> clip) const override {
    asked.push_back({std::string{move}, attacker, defender, clip});
    const Point attackerAt =
        battle_ != nullptr ? battle_->state().fighters.at(attacker).position : Point{};
    return EffectLanding{.frame = frame_,
                         .at = at_.has_value() ? at_ : std::optional{attackerAt},
                         .touch = std::nullopt,
                         .facingTarget = {}};
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
  std::optional<Point> at_;
  const Battle* battle_ = nullptr;
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
          // anim.c: applyRootMomentum: the clip's step along the facing, no other motion, less
          // its move along a way the grace turn found blocked.
          if (fighter.shown.clip == Walk || fighter.shown.clip == WeakWalk) {
            if (fighter.blockedWays == 0) {
              CHECK(moved == doctest::Approx(30.0).epsilon(0.05));
            } else {
              CHECK(moved <= 30.5);
            }
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

TEST_CASE("fighters face each other at the original's angles and walk its fixed-point steps") {
  // The forest fight of the oracle: the enemy at (243, 2565) looks at the partner at (272, 3129) at
  // 2081 (_atan's table), and its first walk steps of 30 and 20 end at z 2594 and 2614, as
  // RotMatrix's cosine (4091, not the 4096 of the vector over its integer length) and the rounding
  // down give them.
  BattleSetup setup = clippedDuel();
  setup.partner.position = {.x = 272, .z = -3129};
  setup.enemies.front().position = {.x = 243, .z = -2565};
  Battle battle = startBattle(std::move(setup), 1);
  const Facing enemy = battle.state().fighters[1].facing;
  CHECK(enemy.x == 207);
  CHECK(enemy.z == -4091);
  const Facing partner = battle.state().fighters[PartnerId].facing;
  CHECK(partner.x == -207);
  CHECK(partner.z == 4091);
  FighterClips clips = madeUpClips();
  clips.at(Walk) = {
      .steps = {RootStep{.side = 0, .forward = 30}, RootStep{.side = 0, .forward = 20}},
      .repeatFrom = 0};
  battle.setClips(1, clips);
  std::vector<std::int32_t> stops;
  Point before = battle.state().fighters[1].position;
  while (stops.size() < 2 && battle.state().frame < 200) {
    battle.step();
    const Point at = battle.state().fighters[1].position;
    if (at.z != before.z) {
      stops.push_back(-at.z);
    }
    before = at;
  }
  CHECK(stops == std::vector<std::int32_t>{2594, 2614});
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

  [[nodiscard]] std::optional<EffectLanding>
  landing(std::string_view /*move*/, FighterId /*attacker*/, FighterId /*defender*/,
          std::optional<std::int32_t> /*clip*/) const override {
    return EffectLanding{.frame = asked_++ == 0 ? first_ : later_,
                         .at = std::nullopt,
                         .touch = std::nullopt,
                         .facingTarget = {}};
  }

private:
  std::int32_t first_;
  std::int32_t later_;
  mutable std::int32_t asked_ = 0;
};

// A made-up flight: the partner's shot flies `frames` long and turns it to its target on the first
// `facing` probe frames (face_target); the enemy's moves touch at once.
class FacingFlights final : public AttackFlights {
public:
  FacingFlights(std::int32_t frames, std::int32_t facing) : frames_(frames), facing_(facing) {}

  [[nodiscard]] std::optional<EffectLanding>
  landing(std::string_view /*move*/, FighterId attacker, FighterId /*defender*/,
          std::optional<std::int32_t> /*clip*/) const override {
    if (attacker != PartnerId) {
      return EffectLanding{
          .frame = 3, .at = std::nullopt, .touch = std::nullopt, .facingTarget = {}};
    }
    std::vector<std::int32_t> facing(static_cast<std::size_t>(facing_));
    std::ranges::iota(facing, 0);
    return EffectLanding{
        .frame = frames_, .at = std::nullopt, .touch = std::nullopt, .facingTarget = facing};
  }

private:
  std::int32_t frames_;
  std::int32_t facing_;
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

namespace {

// Whether the first fighter hit or blocking faces along the line to `point(battle, event)`: toward
// it, or away from it for a hit from behind; nothing when no hit lands.
template <typename PointOf>
std::optional<bool> turnsToward(Battle& battle, PointOf point) {
  while (!battle.finished() && battle.state().frame < 3000) {
    const BattleEvents events = battle.step();
    for (const BattleEvent& event : events) {
      if (event.kind != EventKind::AttackHit && event.kind != EventKind::AttackBlocked) {
        continue;
      }
      const Fighter& defender = battle.state().fighters.at(event.fighter);
      const Point to = point(battle, event);
      const auto dx = static_cast<double>(to.x - defender.position.x);
      const auto dz = static_cast<double>(to.z - defender.position.z);
      const double cross = (defender.facing.x * dz) - (defender.facing.z * dx);
      return std::abs(cross / (std::hypot(dx, dz) * FacingScale)) < 0.01;
    }
  }
  return std::nullopt;
}

} // namespace

TEST_CASE("a fighter hit or blocking turns to the point where the effect touched it") {
  // battle_main.c: BTL_handleHitReaction faces the attack object's position (a box check's
  // instance, BTL_applyBoxAttackHit), and a block looks at it (entityLookAtLocation); in the forest
  // fight the Spit Fire touches at (256, 2987) and the partner turns from 33 to 72.
  const Point point{.x = 400, .z = 300};
  FixedFlights flights{10, point};
  Battle battle = clippedBattle(farShots(), 3);
  flights.setBattle(battle);
  battle.setFlights(&flights);
  CHECK(turnsToward(battle, [&](const Battle&, const BattleEvent&) { return point; }) ==
        std::optional{true});
}

TEST_CASE("a fighter a line or radius check hit turns to its attacker where it stands then") {
  // BTL_calculateAttackHitPosition puts the hit on the defender's body toward the source as the
  // check runs: Sonic Jab turns the partner to the enemy (yaw 860 on frame 221).
  ShiftingFlights flights{10, 10};
  Battle battle = clippedBattle(farShots(), 3);
  battle.setFlights(&flights);
  CHECK(turnsToward(battle, [](const Battle& fight, const BattleEvent& event) {
          return fight.state().fighters.at(*event.other).position;
        }) == std::optional{true});
}

TEST_CASE("an effect that faces its target turns its attacker to it on those frames") {
  // BTL_faceTargetEntity: Spit Fire's tick script turns its attacker to the defender every tick
  // until its counter reaches 11, whatever moved meanwhile. Here the partner's move steps it
  // sideways.
  const auto facingEnemy = [](std::int32_t facingFrames) {
    FacingFlights flights{40, facingFrames};
    Battle battle = clippedBattle(farShots(), 3);
    FighterClips clips = madeUpClips();
    clips.at(FirstMove) = {.steps = std::vector<RootStep>(40, {.side = 25, .forward = 0}),
                           .repeatFrom = std::nullopt};
    battle.setClips(PartnerId, clips);
    battle.setFlights(&flights);
    std::vector<bool> facing;
    while (!battle.finished() && battle.state().frame < 600) {
      battle.step();
      const auto& fighters = battle.state().fighters;
      const bool turning =
          std::ranges::any_of(battle.state().inFlight, [](const AttackInFlight& a) {
            return a.attacker == PartnerId && a.age && *a.age >= 2 && *a.age <= 30;
          });
      if (!turning || fighters[PartnerId].shown.clip != FirstMove) {
        continue;
      }
      const Point from = fighters[PartnerId].position;
      const Point to = fighters[1].position;
      const auto dx = static_cast<double>(to.x - from.x);
      const auto dz = static_cast<double>(to.z - from.z);
      const Facing turned = fighters[PartnerId].facing;
      const double cross = (turned.x * dz) - (turned.z * dx);
      const double dot = (turned.x * dx) + (turned.z * dz);
      // Its facing comes from the original's tables: within a few thousandths of the line.
      facing.push_back(dot > 0 && std::abs(cross / (std::hypot(dx, dz) * FacingScale)) < 0.005);
    }
    return facing;
  };
  const std::vector<bool> facing = facingEnemy(30);
  REQUIRE(facing.size() > 10);
  CHECK(std::ranges::all_of(facing, std::identity{}));
  // Turned on its first probe frame only, it walks off the line.
  CHECK(std::ranges::none_of(facingEnemy(1), std::identity{}));
}

TEST_CASE("a melee approach its target blocks keeps the facing it had") {
  // BTL_tickMeleeAttack looks at its target, steps and tests the next step; blocked by its target,
  // it puts its facing back: in the forest fight the enemy's last step, frame 140, keeps 2091 and
  // only its move, the frame after, looks at the partner (2092). Here the partner slides sideways.
  FacingFlights flights{40, 0};
  Battle battle = clippedBattle(clippedDuel({"shot"}, {"punch"}), 3);
  FighterClips clips = madeUpClips();
  clips.at(FirstMove) = {.steps = std::vector<RootStep>(40, {.side = 25, .forward = 0}),
                         .repeatFrom = std::nullopt};
  battle.setClips(PartnerId, clips);
  battle.setFlights(&flights);
  struct Frame {
    std::int32_t clip = 0;
    Facing facing;
    Point partner;
  };
  std::vector<Frame> frames;
  while (!battle.finished() && battle.state().frame < 1000) {
    battle.step();
    const Fighter& enemy = battle.state().fighters[1];
    frames.push_back({.clip = enemy.shown.clip,
                      .facing = enemy.facing,
                      .partner = battle.state().fighters[PartnerId].position});
  }
  std::int32_t lastSteps = 0;
  std::int32_t kept = 0;
  for (std::size_t i = 1; i + 1 < frames.size(); ++i) {
    if (frames[i].clip == Walk && frames[i + 1].clip == FirstMove &&
        frames[i].partner != frames[i - 1].partner) {
      ++lastSteps;
      kept += frames[i].facing.x == frames[i - 1].facing.x &&
                      frames[i].facing.z == frames[i - 1].facing.z
                  ? 1
                  : 0;
    }
  }
  REQUIRE(lastSteps > 0);
  CHECK(kept == lastSteps);
}

TEST_CASE("a line or radius check hits the body toward its source, at the original's angle") {
  // The forest fight's Sonic Jab, frame 221: the partner at (738, 3494) looked at the enemy at
  // (295, 3381) at 861, and the hit on its body (radius 120) toward the enemy, at (621, 3464),
  // turns it to 860 (BTL_calculateAttackHitPosition, then BTL_handleHitReaction's _atan).
  BattleSetup setup = clippedDuel({"shot"}, {"shot"});
  setup.partner.position = {.x = 738, .z = -3494};
  setup.enemies.front().position = {.x = 295, .z = -3381};
  ShiftingFlights flights{10, 10};
  Battle battle = clippedBattle(std::move(setup), 3);
  battle.setFlights(&flights);
  std::optional<Facing> turned;
  while (!turned && !battle.finished() && battle.state().frame < 3000) {
    for (const BattleEvent& event : battle.step()) {
      if (event.kind == EventKind::AttackHit && event.fighter == PartnerId) {
        turned = battle.state().fighters[PartnerId].facing;
      }
    }
  }
  REQUIRE(turned.has_value());
  CHECK(turned->x == -3967);
  CHECK(turned->z == 1020);
}

namespace {

// A ground with no walls, seen from straight above: 8 units a pixel, the lift up the screen.
class AreaGround final : public BattleGround {
public:
  static constexpr std::int32_t UnitsPerPixel = 8;
  [[nodiscard]] std::optional<ScreenPoint> screenPointOf(Point ground,
                                                         std::int32_t height) const override {
    return ScreenPoint{.x = ground.x / UnitsPerPixel, .y = (ground.z - height) / UnitsPerPixel};
  }
  [[nodiscard]] bool blocksStep(Point /*from*/, Point /*to*/,
                                std::int32_t /*radius*/) const override {
    return false;
  }
};

} // namespace

// map.c: collisionGrace turns a blocked fighter at most 0x50 a frame toward the first free way and
// marks the ways it found blocked (animFlag bits 3 to 6); anim.c: applyRootMomentum drops the move
// along them, so the walk slides along the edge instead of leaning out of the combat area. Without
// that, these battles took a fighter's body up to 247 units (31 pixels) past the area.
TEST_CASE("a fighter turning away from the combat area's edge does not walk out along it") {
  const AreaGround ground;
  const TimingRules& timing = originalRules().timing;
  const std::int32_t halfWidth = timing.combatAreaWidth / 2 * AreaGround::UnitsPerPixel;
  const std::int32_t halfHeight = timing.combatAreaHeight / 2 * AreaGround::UnitsPerPixel;
  std::int32_t worst = 0;
  for (std::uint64_t seed = 1; seed <= 6; ++seed) {
    Battle battle = clippedBattle(clippedDuel(), seed, {.hitFrames = 12, .hitStep = -40});
    battle.setGround(&ground);
    while (!battle.finished() && battle.state().frame < 3000) {
      battle.step();
      for (const Fighter& fighter : battle.state().fighters) {
        // The corners the step test projects: the near ones, on the ground and lifted.
        const std::int32_t radius = fighter.combatant.radius;
        const std::int32_t near = fighter.position.z - radius;
        worst = std::max({worst, std::abs(fighter.position.x) + radius - halfWidth,
                          near - halfHeight, timing.combatAreaCornerHeight - near - halfHeight});
      }
    }
  }
  // At most a walk step and a hit's push of the made-up clips (30 and 40) past it.
  CHECK(worst <= 70);
}
