#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "battle_context.hpp"
#include "usability.hpp"

namespace dwe::game::battle {

namespace {

// A wait ends when its counter drops under this (battle_main.c: cooldown < 2).
constexpr std::int32_t WaitEndsBelow = 2;
constexpr std::int32_t Hundred = 100;

// The freeze ends; the partner's charge stays for the damage of a shot still on its way.
void endFinisher(BattleState& state) {
  state.finisher.fighter.reset();
  state.finisher.auraFrames = 0;
  state.finisher.charging = false;
}

bool frozenFor(const BattleContext& context, FighterId id) {
  return context.state->frozenByFinisher() && context.state->finisher.fighter != id;
}

// battle_main.c: BTL_tickAttackState: a counter aimed at a fighter holds its attack still.
bool heldByCounter(const BattleContext& context, FighterId id) {
  for (FighterId other = 0; other < context.state->fighters.size(); ++other) {
    const Fighter& fighter = context.state->fighters[other];
    if (other != id && fighter.attack && context.moves->at(fighter.attack->move).counter &&
        fighter.attack->target == id) {
      return true;
    }
  }
  return false;
}

// The aura of the finisher counts down; true while it still plays.
bool auraPlaying(BattleContext& context, FighterId id) {
  FinisherState& finisher = context.state->finisher;
  if (finisher.auraFrames <= 0) {
    return false;
  }
  --finisher.auraFrames;
  Fighter& fighter = context.fighter(id);
  if (fighter.target) {
    lookAt(fighter, context.fighter(*fighter.target).position);
  }
  return true;
}

// The finisher's aura starts: everyone else freezes until its attack ends (battle_main.c:
// BTL_startQueuedMove, command_menu.c: BTL_initializeFinisherChargeup).
void beginFinisher(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  FinisherState& finisher = context.state->finisher;
  finisher.fighter = id;
  finisher.auraFrames = context.rules->finisher.auraFrames;
  finisher.charging = false;
  if (id == PartnerId) {
    finisher.charge = 0;
    finisher.charging = true;
    fighter.finisherProgress = 0;
    context.state->commands.buffered = Command::YourCall;
    context.state->commands.current = Command::YourCall;
  }
  if (fighter.target) {
    lookAt(fighter, context.fighter(*fighter.target).position);
  }
  fighter.pose = Pose::Charging;
  context.emit({.kind = EventKind::FinisherStarted,
                .fighter = id,
                .other = fighter.target,
                .move = fighter.queuedMove,
                .amount = 0,
                .status = StatusEffect::None,
                .command = Command::None});
}

// battle_main.c: BTL_isMoveOnCooldown: a move of the shared group waits while one of the group just
// ran.
bool sharedCooldownBlocks(const BattleContext& context, MoveRef move) {
  return context.moves->at(move).sharedCooldown && context.state->sharedCooldown > 0;
}

// The flattened fighter's shot: its own short animation and no cost (battle_main.c:
// BTL_startQueuedMove, BTL_tickFighterAction).
void beginFlattenedShot(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  const PacingRules& pacing = context.rules->pacing;
  fighter.attack = ActiveAttack{.move = context.moves->flattenedShot(),
                                .target = fighter.target,
                                .frame = 0,
                                .length = pacing.flattenedShotFrames,
                                .releaseFrame = pacing.flattenedShotReleaseFrame,
                                .released = false,
                                .followed = false};
  fighter.pose = Pose::Attacking;
  context.emit({.kind = EventKind::AttackStarted,
                .fighter = id,
                .other = fighter.target,
                .move = fighter.attack->move,
                .amount = 0,
                .status = StatusEffect::None,
                .command = Command::None});
}

// battle_main.c: BTL_tickMeleeAttack: walks up to the target and strikes in contact.
void tickMeleeAttack(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (!fighter.target) {
    walkForward(context, id);
    return;
  }
  const Fighter& target = context.fighter(*fighter.target);
  const bool inContact = distanceSquared(fighter.position, target.position) <=
                         contactRangeSquared(context, id, *fighter.target);
  if (!inContact && !context.state->frozenByFinisher()) {
    approach(context, id, target.position);
    return;
  }
  if (inContact) {
    standIdle(fighter);
  }
  const std::optional<MoveRef> queued = fighter.queuedMove;
  if (!queued) {
    return;
  }
  if (context.state->frozenByFinisher()) {
    if (frozenFor(context, id) || auraPlaying(context, id)) {
      return;
    }
  } else if (context.moves->at(*queued).finisher) {
    beginFinisher(context, id);
    return;
  }
  if (sharedCooldownBlocks(context, *queued)) {
    return;
  }
  beginAttack(context, id);
}

// battle_main.c: BTL_tickRangedAttack: keeps the move's distance, then shoots.
void tickRangedAttack(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  const PacingRules& pacing = context.rules->pacing;
  if (fighter.outOfRangeFrames > pacing.rangedPatienceFrames) {
    startQueuedMove(context, id);
    fighter.outOfRangeFrames = 0;
    return;
  }
  if (context.state->finisher.fighter == id || !fighter.target ||
      (!fighter.queuedMove && !fighter.status.flattened)) {
    startQueuedMove(context, id);
    return;
  }
  const Fighter& target = context.fighter(*fighter.target);
  const MoveRef move = fighter.queuedMove && !fighter.status.flattened
                           ? *fighter.queuedMove
                           : context.moves->flattenedShot();
  const std::int64_t total =
      context.moves->at(move).distanceSquared + contactRangeSquared(context, id, *fighter.target);
  const std::int64_t band = total * pacing.rangedBandPercent / Hundred;
  const std::int64_t distance = distanceSquared(fighter.position, target.position);
  if (distance > total + band) {
    walkToward(context, id, target.position);
    ++fighter.outOfRangeFrames;
    return;
  }
  if (distance < total - band) {
    backAwayFrom(context, id, *fighter.target);
    ++fighter.outOfRangeFrames;
    return;
  }
  fighter.outOfRangeFrames = 0;
  standIdle(fighter);
  if (fighter.status.flattened && context.state->frame % pacing.flattenedShotEveryFrames != 0) {
    lookAt(fighter, target.position);
    return;
  }
  startQueuedMove(context, id);
}

// The queued move by its reach (battle_main.c: BTL_tickQueuedMove and the same switch in
// BTL_tickConfusedState).
void tickByRange(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (!fighter.queuedMove) {
    standIdle(fighter);
    return;
  }
  switch (static_cast<MoveRange>(fighter.moveRange)) {
  case MoveRange::Melee:
    tickMeleeAttack(context, id);
    break;
  case MoveRange::Ranged:
  case MoveRange::Area:
    tickRangedAttack(context, id);
    break;
  case MoveRange::Self:
    standIdle(fighter);
    startQueuedMove(context, id);
    break;
  case MoveRange::None:
    break;
  }
}

// The pacing of a fighter waiting for its move: by its charge mode it keeps its distance, stands or
// backs away.
void paceWhileWaiting(BattleContext& context, FighterId id, bool standInNormalMode) {
  Fighter& fighter = context.fighter(id);
  if (!fighter.target) {
    standIdle(fighter);
    return;
  }
  const Point targetAt = context.fighter(*fighter.target).position;
  switch (fighter.chargeMode) {
  case ChargeMode::Aggressive:
    keepDistance(context, id, *fighter.target);
    break;
  case ChargeMode::Normal:
    if (standInNormalMode) {
      standIdle(fighter);
      lookAt(fighter, targetAt);
    } else {
      keepDistance(context, id, *fighter.target);
    }
    break;
  case ChargeMode::Cautious:
    backAwayFrom(context, id, *fighter.target);
    break;
  }
}

// battle_main.c: BTL_tickChargeState: waits until the speed reserve allows the queued move.
void tickChargeState(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  const bool moved = id == PartnerId && followMoveCommand(context);
  if (fighter.cooldown != 0) {
    if (!moved) {
      paceWhileWaiting(context, id, true);
    }
    if (fighter.cooldown < WaitEndsBelow) {
      fighter.planned = false;
      fighter.chargingUp = false;
      fighter.cooldown = 0;
    }
    return;
  }
  if (moved) {
    return;
  }
  const std::int32_t cap = context.rules->speedReserve.cap;
  switch (fighter.chargeMode) {
  case ChargeMode::Aggressive:
    standIdle(fighter);
    if (fighter.target) {
      lookAt(fighter, context.fighter(*fighter.target).position);
    }
    if (fighter.speedReserve > 0) {
      fighter.chargingUp = false;
    }
    break;
  case ChargeMode::Normal: {
    paceWhileWaiting(context, id, false);
    const std::int32_t power =
        fighter.queuedMove ? context.moves->at(*fighter.queuedMove).power : 0;
    if (fighter.speedReserve == cap || fighter.speedReserve >= power) {
      fighter.chargingUp = false;
    }
    break;
  }
  case ChargeMode::Cautious:
    paceWhileWaiting(context, id, false);
    if (fighter.speedReserve == cap) {
      fighter.chargingUp = false;
    }
    break;
  }
}

// battle_main.c: BTL_tickCooldownState: the pause after an attack.
void tickCooldownState(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  paceWhileWaiting(context, id, false);
  if (fighter.cooldown < WaitEndsBelow) {
    fighter.planned = false;
    fighter.coolingDown = false;
    fighter.cooldown = 0;
  }
}

// battle_main.c: BTL_tickConfusedState: wanders and swings at whatever is in front.
void tickConfusedState(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (fighter.coolingDown || fighter.chargingUp) {
    turnAtRandom(context, fighter);
    walkForward(context, id);
    if (fighter.cooldown < WaitEndsBelow) {
      fighter.coolingDown = false;
      fighter.cooldown = 0;
    }
    return;
  }
  if (!fighter.target) {
    turnAtRandom(context, fighter);
    walkForward(context, id);
    if (!fighter.queuedMove ||
        !context.random->chance(context.rules->partnerAi.confusedSwingPercent)) {
      return;
    }
    standIdle(fighter);
    startQueuedMove(context, id);
    return;
  }
  tickByRange(context, id);
}

// battle_main.c: BTL_tickSenileState: the fighter ignores everything for a while.
void tickDisobeyState(Fighter& fighter) {
  --fighter.disobeyFrames;
  if (fighter.disobeyFrames <= 0) {
    fighter.disobeyFrames = 0;
    fighter.planned = false;
    fighter.disobeying = false;
    standIdle(fighter);
    return;
  }
  fighter.pose = Pose::Disobeying;
}

// battle_main.c: BTL_tickAttackState.
void tickAttackState(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (heldByCounter(context, id) || !fighter.attack) {
    return;
  }
  ActiveAttack& attack = *fighter.attack;
  ++attack.frame;
  if (!attack.released && attack.frame >= attack.releaseFrame) {
    attack.released = true;
    releaseAttack(context, id);
  }
  if (attack.frame >= attack.length) {
    endAnimation(context, id);
  }
}

// battle_main.c: BTL_tickHitState: the reaction holds still while the fighter cannot be hit, then
// plays.
// With the fighter's clips, the reaction lasts as long as its clip plays.
void tickHitState(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (fighter.invulnerableFrames > 0) {
    --fighter.invulnerableFrames;
    return;
  }
  const std::int32_t clip = fighter.blocking        ? BlockClip
                            : fighter.hitFromBehind ? HitFromBehindClip
                                                    : HitClip;
  if (const std::optional<bool> ended = clipEnded(fighter, clip);
      ended && fighter.shown.clip == clip) {
    if (*ended) {
      endAnimation(context, id);
    }
    return;
  }
  if (fighter.poseFrames > 0) {
    --fighter.poseFrames;
  }
  if (fighter.poseFrames == 0) {
    endAnimation(context, id);
  }
}

void tickFighter(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (fighter.fainted) {
    // With the fighter's clips, the faint lasts as long as its clip plays.
    const std::optional<bool> ended = clipEnded(fighter, FaintClip);
    if (fighter.pose == Pose::Fainting &&
        (ended && fighter.shown.clip == FaintClip ? *ended : --fighter.poseFrames <= 0)) {
      fighter.poseFrames = 0;
      fighter.pose = Pose::Fainted;
    }
    return;
  }
  testRootStep(context, id);
  if (fighter.attack) {
    tickAttackState(context, id);
    return;
  }
  if (fighter.knockedBack || fighter.blocking) {
    tickHitState(context, id);
    return;
  }
  if (fighter.moveRange == -1) {
    return;
  }
  if (fighter.status.stunned && !fighter.status.flattened) {
    fighter.pose = Pose::Stunned;
    return;
  }
  if (frozenFor(context, id)) {
    standIdle(fighter);
    return;
  }
  if (fighter.status.flattened) {
    if (id != PartnerId || !followMoveCommand(context)) {
      tickRangedAttack(context, id);
    }
  } else if (fighter.status.confused) {
    tickConfusedState(context, id);
  } else if (fighter.disobeying) {
    tickDisobeyState(fighter);
  } else if (fighter.chargingUp) {
    tickChargeState(context, id);
  } else if (fighter.coolingDown) {
    tickCooldownState(context, id);
  } else if (id != PartnerId || !followMoveCommand(context)) {
    tickByRange(context, id);
  }
}

} // namespace

bool followMoveCommand(BattleContext& context) {
  if (context.state->frozenByFinisher()) {
    return false;
  }
  Fighter& partner = context.state->partner();
  switch (context.state->commands.current) {
  case Command::Flee:
    walkToward(context, PartnerId, context.state->tamer);
    return true;
  case Command::Defend:
    standIdle(partner);
    if (partner.target) {
      lookAt(partner, context.fighter(*partner.target).position);
    }
    return true;
  case Command::Distance: {
    if (!partner.target) {
      standIdle(partner);
      return true;
    }
    const Fighter& target = context.fighter(*partner.target);
    std::int64_t reach = 0;
    for (const std::optional<MoveRef>& move : target.moves) {
      if (move) {
        reach = std::max(reach, context.moves->at(*move).distanceSquared);
      }
    }
    reach += context.rules->spacing.keepAwayMarginSquared;
    if (isWalking(partner)) {
      reach += context.rules->spacing.keepAwayWalkingSquared;
    }
    if (distanceSquared(partner.position, target.position) < reach) {
      backAwayFrom(context, PartnerId, *partner.target);
    } else {
      standIdle(partner);
      lookAt(partner, target.position);
    }
    return true;
  }
  default:
    return false;
  }
}

void tickFighterStates(BattleContext& context) {
  BattleState& state = *context.state;
  if (state.sharedCooldown > 0) {
    --state.sharedCooldown;
  }
  for (FighterId id = 0; id < state.fighters.size(); ++id) {
    addFinisherProgress(context, id, context.rules->finisher.perFrame);
    tickFighter(context, id);
  }
  // BTL_tickAttackHits takes the hits the effects registered on the frame before: the effects
  // touch their targets later in the frame (BTL_battleTickFrame: tickObjects, addAttackObject), so
  // a touch on frame t lands on frame t + 1.
  tickAttackHits(context);
  tickAttacksInFlight(context);
  tickStatusEffects(context);
}

void startQueuedMove(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (!fighter.queuedMove) {
    return;
  }
  const MoveRef move =
      fighter.status.flattened ? context.moves->flattenedShot() : *fighter.queuedMove;
  if (context.state->frozenByFinisher()) {
    if (frozenFor(context, id) || auraPlaying(context, id)) {
      return;
    }
  } else if (context.moves->at(move).finisher) {
    beginFinisher(context, id);
    return;
  }
  if (sharedCooldownBlocks(context, move)) {
    return;
  }
  if (fighter.target && *fighter.target != id) {
    const Fighter& target = context.fighter(*fighter.target);
    if (target.knockedBack) {
      return;
    }
    lookAt(fighter, target.position);
  }
  if (context.moves->at(move).sharedCooldown) {
    context.state->sharedCooldown = context.rules->moves.sharedCooldownFrames;
  }
  if (fighter.status.flattened) {
    beginFlattenedShot(context, id);
  } else {
    beginAttack(context, id);
  }
}

void beginAttack(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  if (!fighter.queuedMove) {
    return;
  }
  const MoveRef ref = *fighter.queuedMove;
  const BattleMove& move = context.moves->at(ref);
  // battle_main.c: BTL_setupMoveExecution.
  if (move.range == MoveRange::Self) {
    fighter.target = id;
  } else if (!fighter.target && move.needsTarget) {
    if (id != PartnerId) {
      setTarget(context, id, PartnerId);
    } else {
      for (FighterId enemy = 1; enemy < context.state->fighters.size(); ++enemy) {
        if (!context.fighter(enemy).defeated()) {
          setTarget(context, id, enemy);
        }
      }
    }
  }
  if (id == PartnerId) {
    if (!move.finisher) {
      fighter.pendingMp +=
          partnerMpCost(*context.rules, move.mpCost, fighter.stats.brain,
                        context.condition->sick || context.condition->injured, MpCostUse::Payment);
    }
  } else {
    const bool free =
        context.state->scripted && context.rules->quirks.on(quirk::ScriptedEnemiesFreeMp);
    if (!move.finisher && !free) {
      fighter.mp -= move.mpCost;
    }
    std::vector<MoveRef>& seen = context.state->seenMoves;
    if (move.learnable && std::ranges::find(seen, ref) == seen.end() &&
        std::cmp_less(seen.size(), context.rules->rewards.trackedMoves)) {
      seen.push_back(ref);
    }
  }
  if (move.range == MoveRange::Self && fighter.buffsLeft != 0) {
    --fighter.buffsLeft;
  }
  fighter.speedReserve =
      std::max(fighter.speedReserve - move.power, context.rules->moves.speedReserveFloor);
  // With the fighter's clips, the attack lasts as long as the move's clip plays
  // (BTL_tickFighterAction waits for animFlag bit 0 to go off).
  const TimingRules& timing = context.rules->timing;
  const std::optional<std::int32_t> clip = moveClipOf(context, fighter, ref);
  const std::int32_t length =
      clip ? clipLength(fighter, *clip).value_or(timing.attackFrames) : timing.attackFrames;
  // A technique whose effect the battle can follow leaves with the attack: the effect starts with
  // it (BTL_setupMoveExecution, BTL_startEFE) and its touch, not the clip's hit frame, decides when
  // it lands. The original has no other hit: a melee move only waits for contact to start
  // (BTL_tickMeleeAttack) and its effect's checks (apply_line_hit for Sonic Jab...) strike.
  const bool followed = move.range != MoveRange::Self && fighter.target && *fighter.target != id &&
                        effectLanding(context, id, *fighter.target, ref, clip).has_value();
  fighter.attack = ActiveAttack{
      .move = ref,
      .target = fighter.target,
      .frame = 0,
      .length = length,
      .releaseFrame = followed ? 0 : std::min(timing.hitFrame, std::max(length - 1, 0)),
      .released = false,
      .followed = followed};
  fighter.pose = Pose::Attacking;
  context.emit({.kind = EventKind::AttackStarted,
                .fighter = id,
                .other = fighter.target,
                .move = ref,
                .amount = 0,
                .status = StatusEffect::None,
                .command = Command::None});
  if (followed) {
    fighter.attack->released = true;
    releaseAttack(context, id);
  }
}

void endAnimation(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  FinisherState& finisher = context.state->finisher;
  if (finisher.fighter == id) {
    if (finisher.auraFrames > 0) {
      return;
    }
    endFinisher(*context.state);
  }
  if (fighter.attack) {
    if (id == PartnerId) {
      ++context.state->counters.attacks;
    }
    fighter.chasingAttacker = false;
    fighter.coolingDown = true;
    fighter.cooldown = context.rules->pacing.afterAttackFrames;
    addFinisherProgress(context, id,
                        fighter.finisherGoal * context.rules->finisher.perAttack /
                            context.rules->finisher.progressDivisor);
  }
  if (fighter.invulnerableFrames <= 0) {
    if (!fighter.attack) {
      fighter.struck = false;
    }
    if (fighter.blocking) {
      fighter.blocking = false;
    } else {
      fighter.knockedBack = false;
      fighter.attack.reset();
      fighter.planned = false;
    }
  }
  if (!fighter.knockedBack) {
    if (fighter.status.flatFrames == -1) {
      fighter.status.flatFrames = context.rules->status.flattenTransitionFrames;
    }
  } else {
    fighter.disobeyFrames = 0;
    fighter.disobeying = false;
  }
  if (!fighter.attack && !fighter.knockedBack && !fighter.blocking) {
    standIdle(fighter);
  }
}

void faint(BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  fighter.struck = true;
  fighter.fainted = true;
  fighter.status.flattened = false;
  fighter.status.flatFrames = 0;
  fighter.status.poisoned = false;
  fighter.status.confused = false;
  fighter.status.stunned = false;
  fighter.knockedBack = false;
  fighter.attack.reset();
  fighter.blocking = false;
  fighter.planned = true;
  fighter.moveRange = -1;
  resetAction(fighter);
  fighter.pose = Pose::Fainting;
  fighter.poseFrames = context.rules->timing.faintFrames;
  context.emit({.kind = EventKind::Fainted,
                .fighter = id,
                .other = std::nullopt,
                .move = std::nullopt,
                .amount = 0,
                .status = StatusEffect::None,
                .command = Command::None});
}

void standIdle(Fighter& fighter) {
  fighter.collidedWhileBacking = false;
  fighter.pose = Pose::Standing;
  fighter.poseFrames = 0;
}

void addFinisherProgress(BattleContext& context, FighterId id, std::int32_t amount) {
  Fighter& fighter = context.fighter(id);
  const std::optional<MoveRef> finisher = fighter.moves.at(FinisherSlot);
  if (!finisher || !context.moves->at(*finisher).finisher ||
      fighter.finisherProgress >= fighter.finisherGoal) {
    return;
  }
  fighter.finisherProgress = std::min(fighter.finisherProgress + amount, fighter.finisherGoal);
  if (fighter.finisherProgress == fighter.finisherGoal) {
    context.emit({.kind = EventKind::FinisherReady,
                  .fighter = id,
                  .other = std::nullopt,
                  .move = std::nullopt,
                  .amount = 0,
                  .status = StatusEffect::None,
                  .command = Command::None});
  }
}

} // namespace dwe::game::battle
