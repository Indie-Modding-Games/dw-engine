#include <algorithm>
#include <cstdint>

#include "battle_context.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t WholePercent = 100;

void emitPartnerEvent(BattleContext& context, EventKind kind, std::int32_t amount) {
    context.emit({.kind = kind,
                  .fighter = PartnerId,
                  .other = std::nullopt,
                  .move = std::nullopt,
                  .amount = amount,
                  .status = StatusEffect::None,
                  .command = Command::None});
}

// main.c: handleBuffDisks: each stat goes up by its amount, but not past the limit taken from its value when the
// battle started (INITIAL_COMBAT_STATS); BTL_buffStats keeps it within the cap.
void boost(Fighter& partner, const StatBoost& disk) {
    const auto raise = [&disk](std::int32_t& stat, std::int32_t start, std::int32_t amount) {
        if (amount == 0) {
            return;
        }
        std::int32_t gain = amount;
        if (!disk.ignoresLimit) {
            const std::int32_t limit = std::min(start * disk.limitPercent / WholePercent, disk.limitCap);
            gain = std::min(amount, limit - stat);
        }
        stat = std::min(stat + gain, disk.limitCap);
    };
    raise(partner.stats.offense, partner.startStats.offense, disk.offense);
    raise(partner.stats.defense, partner.startStats.defense, disk.defense);
    raise(partner.stats.speed, partner.startStats.speed, disk.speed);
}

// item.c: the item's handler, now that it reached the partner.
void land(BattleContext& context, const ThrownItem& item) {
    Fighter& partner = context.fighter(PartnerId);
    if (item.consciousOnly && partner.fainted) {
        return;
    }
    if (item.reviveHpPercent) {
        // item.c: handleRestore: a fallen partner gets up; a standing one is healed by the same share.
        const std::int32_t hp = partner.stats.maxHp * *item.reviveHpPercent / WholePercent;
        if (partner.fainted) {
            revivePartner(context, hp);
        } else {
            restorePartner(partner, hp, 0);
        }
    }
    restorePartner(partner, item.hp, item.mp);
    if (item.curesStatus) {
        cureStatus(context, PartnerId);
    }
    if (item.protects) {
        partner.status.protectedFromStatus = true;
    }
    if (item.boost) {
        boost(partner, *item.boost);
    }
}

} // namespace

void restorePartner(Fighter& partner, std::int32_t hp, std::int32_t mp) {
    if (partner.fainted) {
        return;
    }
    partner.hp = std::min(partner.hp + hp, partner.stats.maxHp);
    partner.mp = std::min(partner.mp + mp, partner.stats.maxMp);
}

void revivePartner(BattleContext& context, std::int32_t hp) {
    BattleState& state = *context.state;
    Fighter& partner = state.partner();
    // battle_setup.c: BTL_isBattleFinished, at the end of the revive animation (its length is not modelled).
    partner.fainted = false;
    partner.struck = false;
    partner.hp = std::clamp(hp, 1, partner.stats.maxHp);
    partner.pendingDamage = 0;
    standIdle(partner);
    state.deathFrames = 0;
    state.finisher.fighter.reset();
    state.finisher.auraFrames = 0;
    state.finisher.charging = false;
    for (Fighter& fighter : state.fighters) {
        resetAction(fighter);
        fighter.planned = false;
        fighter.chasingAttacker = false;
        if (!fighter.fainted || !fighter.defeated()) {
            fighter.moveRange = 0;
        }
    }
    emitPartnerEvent(context, EventKind::Revived, partner.hp);
}

void throwItem(BattleContext& context, const ThrownItem& item) {
    context.state->bagOpen = false;
    context.state->thrownItem = ThrownItemState{.item = item, .frames = 0};
    emitPartnerEvent(context, EventKind::ItemThrown, 0);
}

void tickThrownItem(BattleContext& context) {
    std::optional<ThrownItemState>& thrown = context.state->thrownItem;
    if (!thrown) {
        return;
    }
    const ItemThrowRules& rules = context.rules->itemThrow;
    if (++thrown->frames < rules.releaseFrame + rules.flightFrames) {
        return;
    }
    const ThrownItem item = thrown->item;
    thrown.reset();
    land(context, item);
    emitPartnerEvent(context, EventKind::ItemLanded, 0);
}

void tickHpBars(BattleContext& context) {
    const std::int32_t shownFrames = context.rules->hud.enemyHpBarFrames;
    for (const BattleEvent& event : *context.events) {
        const bool damaged = event.kind == EventKind::AttackHit || event.kind == EventKind::AttackBlocked ||
                             event.kind == EventKind::PoisonDamage;
        if (damaged && event.fighter != PartnerId) {
            context.fighter(event.fighter).hpBarFrames = 0;
        }
    }
    for (Fighter& fighter : context.state->fighters) {
        if (fighter.hpBarFrames < shownFrames && !fighter.knockedBack && fighter.pendingDamage == 0) {
            ++fighter.hpBarFrames;
        }
    }
}

} // namespace dwe::game::battle
