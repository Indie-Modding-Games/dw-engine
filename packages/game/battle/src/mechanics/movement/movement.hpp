#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/battle_context.hpp"

namespace dwe::game::battle {

// Walking on the battle ground. Distances squared, as the original compares them.
std::int64_t contactRangeSquared(const BattleContext& context, FighterId a, FighterId b);
// map.c: entityLookAtLocation: the facing from `from` toward `to`, at the angle math.c's _atan
// gives (its table, the ratio rounded down) with RotMatrix's sine and cosine.
Facing facingToward(Point from, Point to);
// A root motion sum in FacingScale parts of a unit, in whole units as the original's location
// takes it (anim.c: applyRootMomentum shifts its fixed-point sum right): rounded down along the
// original's axes, so up along world z, which runs the other way.
Point wholeUnits(std::int64_t x, std::int64_t z);
// battle_effect.c: BTL_calculateAttackHitPosition: the point of a body at `at` of `radius` toward
// `toward`, where a radius or line check hits it.
Point bodyPointToward(Point at, Point toward, std::int32_t radius);
void lookAt(Fighter& fighter, Point point);
// battle_main.c: BTL_handleHitReaction: a fighter hit from the front (within a quarter turn of
// where it looks) or while flattened turns to the attack; one hit from behind turns its back to it.
// True for the latter.
bool turnForHit(Fighter& fighter, Point attack);
// The walks stop at whatever the step would run into (map.c: entityCheckCollision): the combat
// area, the map and the other fighters. battle_main.c: BTL_moveTowardLocation: blocked, the fighter
// keeps its facing and turns a little toward a free way (collisionGrace).
void walkToward(BattleContext& context, FighterId id, Point point);
// battle_main.c: BTL_tickMeleeAttack walks up to the target: its step is not held, but if the
// collision test blocks it the fighter keeps its old facing (and collisionGrace, without the
// target, may turn it).
void approach(BattleContext& context, FighterId id, FighterId target);
// battle_main.c: BTL_backAwayFromTarget: turns its back on the target and walks away.
void backAwayFrom(BattleContext& context, FighterId id, FighterId target);
// battle_main.c: BTL_maintainTargetDistance and BTL_maintainDistanceRange: nearer than the contact
// reach plus the near band it backs away, farther than the reach plus the far band it walks up,
// in between it stands and looks at the target (the walking band while it walks).
void keepDistance(BattleContext& context, FighterId id, FighterId target);
// battle_main.c: BTL_confusedRotate: a confused fighter turns at random.
void turnAtRandom(BattleContext& context, Fighter& fighter);
// Walks the way it faces.
void walkForward(BattleContext& context, FighterId id);
bool isWalking(const Fighter& fighter);
// battle_main.c: BTL_tickFighterAction: a fighter with clips holds this frame's root step when the
// step ahead runs into something (entityCheckCollision with the combat area).
void testRootStep(BattleContext& context, FighterId id);
// anim.c: tickAnimation for every fighter with clips: its clip plays a frame (unless frozen) and
// its root step moves it along its facing, unless held (applyRootMomentum).
void tickRootMotion(BattleContext& context);
// map.c: entityCheckCombatArea: whether the step of a mover of `radius` from `from` to `to` leaves
// the rectangle of width x height pixels around the screen's centre, moving further out.
bool leavesScreenArea(const BattleGround& ground, const TimingRules& timing, Point from, Point to,
                      std::int32_t radius, std::int32_t width, std::int32_t height);
// The integer square root, rounded down.
std::int64_t integerSqrt(std::int64_t value);

} // namespace dwe::game::battle
