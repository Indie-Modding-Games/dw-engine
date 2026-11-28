#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "mechanics/actions/actions.hpp"
#include "mechanics/decisions/decisions.hpp"
#include "mechanics/decisions/usability.hpp"
#include "mechanics/status/status.hpp"

namespace dwe::game::battle {

namespace {

// BTL_tickEnemyAI projects an enemy's node 1 and tests it with isOffScreen(out, 0xc8, 0xa0): off
// when more than half of 200 across or 160 down from the screen's centre.
constexpr std::uint32_t EnemyBodyNode = 1;
constexpr std::int32_t OffScreenHalfWidth = 0xc8 / 2;
constexpr std::int32_t OffScreenHalfHeight = 0xa0 / 2;

// Unknown (no ground, or it does not draw the fighter): on screen, so the enemy rolls.
bool enemyOffScreen(const BattleContext& context, FighterId id) {
  if (context.ground == nullptr) {
    return false;
  }
  const std::optional<ScreenPoint> body = context.ground->fighterNodeOnScreen(id, EnemyBodyNode);
  return body && (body->x < -OffScreenHalfWidth || body->x > OffScreenHalfWidth ||
                  body->y < -OffScreenHalfHeight || body->y > OffScreenHalfHeight);
}

} // namespace

void tickEnemy(BattleContext& context, FighterId id, bool& stop) {
  Fighter& enemy = context.fighter(id);
  if (!context.state->frozenByFinisher()) {
    tickTimers(context, id, 0);
  }
  if (enemy.knockedBack || enemy.attack || enemy.blocking || enemy.fainted) {
    return;
  }
  if (enemy.hp == 0) {
    faint(context, id);
    return;
  }
  if (enemy.defeated()) {
    standIdle(enemy);
    enemy.moveRange = -1;
    resetFlatten(enemy);
    removeStatusVisuals(context, id);
    clearStatusFlags(enemy);
    return;
  }
  if (playersDown(*context.state)) {
    standIdle(enemy);
    resetFlatten(enemy);
    enemy.knockedBack = false;
    enemy.blocking = false;
    enemy.planned = true;
    enemy.moveRange = -1;
    return;
  }
  if (context.state->frozenByFinisher()) {
    stop = true;
    return;
  }
  if (enemy.planned) {
    return;
  }
  if (enemy.status.flattened) {
    enemy.queuedMove = context.moves->flattenedShot();
    setTarget(context, id, enemyTargetOf(context, id));
    enemy.moveRange = static_cast<std::int32_t>(MoveRange::Ranged);
    enemy.planned = true;
    return;
  }
  if (enemy.status.stunned) {
    return;
  }
  if (enemy.status.confused) {
    chooseConfusedMove(context, id);
    return;
  }
  if (enemy.disobeying || enemy.chargingUp || enemy.coolingDown) {
    return;
  }
  if (enemy.status.flatFrames == 0 && enemy.hasFinisher() &&
      enemy.finisherProgress == enemy.finisherGoal) {
    setTarget(context, id, enemyTargetOf(context, id));
    queueMove(context, id, FinisherSlot);
    enemy.finisherProgress = 0;
    return;
  }
  if (!enemy.chasingAttacker) {
    setTarget(context, id, enemyTargetOf(context, id));
  }
  if (context.state->matchClock) {
    // std_main.c: STD_tickEnemyAI: a tournament rival is cautious one time in ten, otherwise it
    // rolls aggressive or normal.
    enemy.chargeMode = context.random->below(context.rules->enemyAi.matchCautiousRoll) == 0
                           ? ChargeMode::Cautious
                           : static_cast<ChargeMode>(context.random->below(2));
  } else if (enemyOffScreen(context, id)) {
    // battle_main.c:631-640: an enemy whose body (node 1) is off a 200 x 160 box in the middle of
    // the screen (map.c: isOffScreen) is cautious, with no roll.
    enemy.chargeMode = ChargeMode::Cautious;
  } else {
    enemy.chargeMode = static_cast<ChargeMode>(context.random->below(2));
  }
  const std::array<bool, EquippedSlots> usable = usableSlots(context.view(), id);
  if (!anyUsable(usable)) {
    enemy.chargeMode = ChargeMode::Cautious;
    waitWithoutMove(context, id);
    return;
  }
  const MoveDecision decision = context.strategies->enemyAi->chooseMove(context.view(), id, usable);
  if (decision.slot) {
    queueMove(context, id, *decision.slot);
  } else {
    waitWithoutMove(context, id);
  }
}

bool playersDown(const BattleState& state) {
  if (state.allies.empty()) {
    return state.partner().hp == 0;
  }
  return std::ranges::all_of(state.partnersIn(),
                             [&state](FighterId id) { return state.fighters.at(id).hp == 0; });
}

void decayBuffPriorities(BattleContext& context) {
  BattleState& state = *context.state;
  if (state.frame % context.rules->partnerAi.buffPriorityDecayFrames == 0) {
    for (Fighter& fighter : state.fighters) {
      if (fighter.buffPriority != 0) {
        --fighter.buffPriority;
      }
    }
  }
}

void tickEnemyDecisions(BattleContext& context) {
  decayBuffPriorities(context);
  BattleState& state = *context.state;
  bool stop = false;
  for (FighterId id = 1; id < state.fighters.size() && !stop; ++id) {
    if (!state.fighters[id].gone() && !state.commanded(id)) {
      tickEnemy(context, id, stop);
    }
  }
}

} // namespace dwe::game::battle
