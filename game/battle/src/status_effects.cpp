#include <algorithm>
#include <cstdint>

#include "battle_context.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;

std::int32_t rollBetween(ruleset::Random& random, std::int32_t min, std::int32_t max) {
    return random.below(max - min + 1) + min;
}

// battle_main.c: BTL_clearConfusion.
void clearConfusion(BattleContext& context, FighterId id) {
    Fighter& fighter = context.fighter(id);
    resetAction(fighter);
    selectTarget(context, id, fighter.target, false);
    fighter.status.confused = false;
    fighter.status.confusionFrames = 0;
    if (!fighter.status.stunned && !fighter.status.flattened && fighter.status.flatFrames == 0) {
        fighter.planned = false;
    }
    context.emit({.kind = EventKind::StatusCleared,
                  .fighter = id,
                  .other = std::nullopt,
                  .move = std::nullopt,
                  .amount = 0,
                  .status = StatusEffect::Confusion,
                  .command = Command::None});
}

// battle_main.c: BTL_clearStun.
void clearStun(BattleContext& context, FighterId id) {
    Fighter& fighter = context.fighter(id);
    resetAction(fighter);
    fighter.status.stunned = false;
    fighter.status.stunFrames = 0;
    if (!fighter.status.flattened && fighter.status.flatFrames == 0) {
        fighter.planned = false;
        standIdle(fighter);
    }
    context.emit({.kind = EventKind::StatusCleared,
                  .fighter = id,
                  .other = std::nullopt,
                  .move = std::nullopt,
                  .amount = 0,
                  .status = StatusEffect::Stun,
                  .command = Command::None});
}

void clearPoison(BattleContext& context, FighterId id) {
    Fighter& fighter = context.fighter(id);
    fighter.status.poisoned = false;
    fighter.status.poisonFrames = 0;
    context.emit({.kind = EventKind::StatusCleared,
                  .fighter = id,
                  .other = std::nullopt,
                  .move = std::nullopt,
                  .amount = 0,
                  .status = StatusEffect::Poison,
                  .command = Command::None});
}

void tickPoison(BattleContext& context, FighterId id) {
    Fighter& fighter = context.fighter(id);
    if (!context.state->frozenByFinisher()) {
        --fighter.status.poisonFrames;
    }
    if (fighter.status.poisonFrames != 0) {
        return;
    }
    const StatusRules& rules = context.rules->status;
    fighter.status.poisonFrames = rules.poisonEveryFrames;
    const std::int32_t damage =
        fighter.stats.maxHp * rollBetween(*context.random, rules.poisonMinPercent, rules.poisonMaxPercent) / Hundred;
    fighter.pendingDamage = std::min(fighter.pendingDamage + damage, context.rules->limits.pendingDamageCap);
    context.emit({.kind = EventKind::PoisonDamage,
                  .fighter = id,
                  .other = std::nullopt,
                  .move = std::nullopt,
                  .amount = damage,
                  .status = StatusEffect::None,
                  .command = Command::None});
}

// The squash (moveRange -1 and the struck flag) at the start and the end of being flattened.
void squash(Fighter& fighter) {
    fighter.pose = Pose::Squashed;
    fighter.moveRange = -1;
    fighter.planned = true;
    fighter.struck = true;
}

// battle_main.c: BTL_tickStatusEffects, the flatten part: hit, squash, flattened, squash back, recovered.
void tickFlatten(BattleContext& context, FighterId id) {
    Fighter& fighter = context.fighter(id);
    StatusState& status = fighter.status;
    if (status.flatFrames <= 0) {
        return;
    }
    const StatusRules& rules = context.rules->status;
    if (!context.state->frozenByFinisher() || status.flatFrames <= rules.flattenTransitionFrames) {
        --status.flatFrames;
    }
    const std::int32_t squashAt = rules.flattenTransitionFrames - 1;
    if (status.flattened) {
        if (status.flatFrames == squashAt) {
            if (fighter.knockedBack || fighter.attack) {
                ++status.flatFrames;
            } else {
                squash(fighter);
            }
        } else if (status.flatFrames == 0) {
            status.flattened = false;
            fighter.planned = false;
            fighter.struck = false;
            fighter.moveRange = 0;
            resetAction(fighter);
            standIdle(fighter);
            context.emit({.kind = EventKind::StatusCleared,
                          .fighter = id,
                          .other = std::nullopt,
                          .move = std::nullopt,
                          .amount = 0,
                          .status = StatusEffect::Flatten,
                          .command = Command::None});
        }
        return;
    }
    if (status.flatFrames == squashAt) {
        squash(fighter);
    } else if (status.flatFrames == 0) {
        status.flattened = true;
        status.flatFrames = rollBetween(*context.random, rules.flattenMinFrames, rules.flattenMaxFrames);
        fighter.struck = false;
        fighter.planned = false;
        standIdle(fighter);
    }
}

} // namespace

void applyMoveStatus(BattleContext& context, FighterId id, MoveRef move) {
    Fighter& fighter = context.fighter(id);
    const BattleMove& data = context.moves->at(move);
    if (fighter.status.protectedFromStatus || data.statusChance == 0) {
        return;
    }
    if (context.random->below(Hundred) >= data.statusChance) {
        return;
    }
    const StatusRules& rules = context.rules->status;
    StatusState& status = fighter.status;
    bool applied = false;
    switch (data.status) {
    case StatusEffect::Poison:
        if (!status.poisoned) {
            status.poisoned = true;
            status.poisonFrames = rules.poisonEveryFrames;
            applied = true;
        }
        break;
    case StatusEffect::Confusion:
        if (!status.confused) {
            status.confused = true;
            status.confusionFrames = rollBetween(*context.random, rules.confusionMinFrames, rules.confusionMaxFrames);
            resetAction(fighter);
            applied = true;
        }
        break;
    case StatusEffect::Stun:
        if (!status.stunned) {
            status.stunned = true;
            status.stunFrames = rollBetween(*context.random, rules.stunMinFrames, rules.stunMaxFrames);
            resetAction(fighter);
            applied = true;
        }
        break;
    case StatusEffect::Flatten:
        if (!status.flattened) {
            status.flatFrames = -1;
            resetAction(fighter);
            applied = true;
        }
        break;
    case StatusEffect::None:
        break;
    }
    if (applied) {
        context.emit({.kind = EventKind::StatusApplied,
                      .fighter = id,
                      .other = std::nullopt,
                      .move = move,
                      .amount = 0,
                      .status = data.status,
                      .command = Command::None});
    }
    if (id == PartnerId) {
        ++context.state->counters.statusesTaken;
    }
}

void tickStatusEffects(BattleContext& context) {
    BattleState& state = *context.state;
    const bool waitForPartner = context.rules->quirks.on(quirk::ConfusionWaitsForPartnerAttack);
    for (FighterId id = 0; id < state.fighters.size(); ++id) {
        Fighter& fighter = state.fighters[id];
        if (fighter.fainted) {
            continue;
        }
        if (fighter.status.poisoned) {
            tickPoison(context, id);
        }
        if (fighter.status.confused) {
            if (!state.frozenByFinisher() && fighter.status.confusionFrames != 0) {
                --fighter.status.confusionFrames;
            }
            const Fighter& watched = waitForPartner ? state.partner() : fighter;
            if (fighter.status.confusionFrames == 0 && !watched.attack) {
                clearConfusion(context, id);
            }
        }
        if (fighter.status.stunned) {
            if (!state.frozenByFinisher()) {
                --fighter.status.stunFrames;
            }
            if (fighter.status.stunFrames == 0) {
                clearStun(context, id);
            }
        }
        tickFlatten(context, id);
    }
}

void cureStatus(BattleContext& context, FighterId id) {
    Fighter& fighter = context.fighter(id);
    if (fighter.status.poisoned) {
        clearPoison(context, id);
    }
    if (fighter.status.confused) {
        clearConfusion(context, id);
    }
    if (fighter.status.stunned) {
        clearStun(context, id);
    }
    // battle_main.c: BTL_healFlatten: the flattening skips to its recovery.
    StatusState& status = fighter.status;
    const std::int32_t transition = context.rules->status.flattenTransitionFrames;
    status.flatFrames = std::min(status.flatFrames, transition);
    if (status.flatFrames != 0 && !status.flattened) {
        status.flattened = true;
        status.flatFrames = transition;
    }
}

} // namespace dwe::game::battle
