#include <algorithm>
#include <cstdint>
#include <optional>

#include "battle_context.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t WholePercent = 100;
// item.c: handleRestore, case 0xc: the amount the whole revive adds, capped by the maximum.
constexpr std::int32_t WholeRestoreHp = 9999;

// main.c: ITEM_THROW_HEIGHT: 0x34D - 5 (frame - 11)^2 over the ground.
constexpr std::int32_t ThrowTop = 0x34D;
constexpr std::int32_t ThrowTopFrame = 11;
constexpr std::int32_t ThrowBend = 5;
// main.c: tickThrownItem: each step covers 1 / (0x19 - frame) of the ground left.
constexpr std::int32_t FlightSteps = 0x19;

void emitPartnerEvent(BattleContext& context, EventKind kind, std::int32_t amount,
                      FighterId partner = PartnerId) {
  context.emit({.kind = kind,
                .fighter = partner,
                .other = std::nullopt,
                .move = std::nullopt,
                .amount = amount,
                .status = StatusEffect::None,
                .command = Command::None});
}

// main.c: handleBuffDisks: each stat goes up by its amount, but not past the limit taken from its
// value when the battle started (INITIAL_COMBAT_STATS); BTL_buffStats keeps it within the cap and
// shows the gain over the partner in green with the stat's icon (addEntityText(partner, 0, 0xb,
// gain, 3 to 5)), as it computed it.
void boost(BattleContext& context, FighterId id, Fighter& partner, const StatBoost& disk) {
  const auto raise = [&context, &disk, id](std::int32_t& stat, std::int32_t start,
                                           std::int32_t amount, EventKind shown) {
    if (amount == 0) {
      return;
    }
    std::int32_t gain = amount;
    if (!disk.ignoresLimit) {
      const std::int32_t limit = std::min(start * disk.limitPercent / WholePercent, disk.limitCap);
      gain = std::min(amount, limit - stat);
    }
    stat = std::min(stat + gain, disk.limitCap);
    emitPartnerEvent(context, shown, gain, id);
  };
  raise(partner.stats.offense, partner.startStats.offense, disk.offense, EventKind::OffenseRaised);
  raise(partner.stats.defense, partner.startStats.defense, disk.defense, EventKind::DefenseRaised);
  raise(partner.stats.speed, partner.startStats.speed, disk.speed, EventKind::SpeedRaised);
}

// battle_main.c: BTL_healStatusEffect(1): the rings, in the colour of the last of poison,
// confusion, stun and flattening the partner has, when it has any.
void showCureRings(BattleContext& context, FighterId id, const Fighter& partner) {
  using Kind = StatusVisuals::CureKind;
  const StatusState& status = partner.status;
  std::optional<Kind> kind;
  if (status.poisoned) {
    kind = Kind::Poison;
  }
  if (status.confused) {
    kind = Kind::Confusion;
  }
  if (status.stunned) {
    kind = Kind::Stun;
  }
  if (status.flattened || status.flatFrames != 0) {
    kind = Kind::Flattening;
  }
  if (kind) {
    context.visuals->addBuffRings(id, partner, *kind);
  }
}

// item.c: the item's handler, now that it reached the partner. The healing ones show what they
// heal over the partner (addEntityText(partner, 0, 0xb, value, icon)): the item's own amount, not
// what the maximum let in; handleDoubleFloppy shows the HP and then the MP.
void land(BattleContext& context, const ThrownItem& item, FighterId id) {
  Fighter& partner = context.fighter(id);
  if (item.consciousOnly && partner.fainted) {
    return;
  }
  if (item.reviveHpPercent) {
    // item.c: handleRestore: a fallen partner gets up; a standing one is healed by the same share.
    // The whole revive adds 0x270f, and shows it, rather than a share of the maximum.
    const std::int32_t hp = *item.reviveHpPercent >= WholePercent
                                ? WholeRestoreHp
                                : partner.stats.maxHp * *item.reviveHpPercent / WholePercent;
    if (partner.fainted) {
      revivePartner(context, hp, id);
    } else {
      restorePartner(partner, hp, 0);
    }
    emitPartnerEvent(context, EventKind::HpRestored, hp, id);
  }
  restorePartner(partner, item.hp, item.mp);
  if (item.hp > 0) {
    emitPartnerEvent(context, EventKind::HpRestored, item.hp, id);
  }
  if (item.mp > 0) {
    emitPartnerEvent(context, EventKind::MpRestored, item.mp, id);
  }
  if (item.curesStatus) {
    if (item.cureRings && context.visuals != nullptr) {
      showCureRings(context, id, partner);
    }
    cureStatus(context, id);
  }
  if (item.protects) {
    partner.status.protectedFromStatus = true;
  }
  // item.c: handleHPHealingItem, handleMPHealingItem, handleDoubleFloppy and handleRestore end
  // with addHealingParticleEffect(partner, ring).
  if (item.healingParticles) {
    emitPartnerEvent(context, EventKind::HealingShown, item.healingRing ? 1 : 0, id);
  }
  if (item.boost) {
    boost(context, id, partner, *item.boost);
    // main.c: handleBuffDisks ends with BTL_addBuffDiskEffect, unless the partner is down.
    if (!partner.fainted && context.visuals != nullptr) {
      context.visuals->addBuffDisk(id, partner);
    }
  }
}

// One item on its way: it lands on the flight's last frame (main.c: tickThrownItem).
void tickFlight(BattleContext& context, std::optional<ThrownItemState>& thrown) {
  if (!thrown) {
    return;
  }
  const ItemThrowRules& rules = context.rules->itemThrow;
  ++thrown->frames;
  // The step of the flight's last frame lands it (TAMER_ITEM.time reaches 0x18 after it).
  const std::optional<std::int32_t> flight = flightFrameOf(*thrown, rules);
  if (!flight || *flight < rules.flightFrames - 1) {
    return;
  }
  const ThrownItem item = thrown->item;
  const FighterId target = thrown->target;
  thrown.reset();
  land(context, item, target);
  // main.c: tickThrownItem: BTL_addItemParticles(ENTITY_TABLE[1]) after the item's handler.
  if (context.visuals != nullptr) {
    context.visuals->addItemParticles(target, context.fighter(target));
  }
  emitPartnerEvent(context, EventKind::ItemLanded, 0, target);
}

} // namespace

std::int32_t itemThrowHeight(std::int32_t frame) {
  const std::int32_t fromTop = frame - ThrowTopFrame;
  return ThrowTop - (ThrowBend * fromTop * fromTop);
}

ItemFlight stepItemFlight(const ItemFlight& flight, Point partner, std::int32_t frame) {
  ItemFlight next{.point = flight.point, .height = itemThrowHeight(frame)};
  if (frame <= 0) {
    return next;
  }
  const std::int32_t steps = FlightSteps - frame;
  if (steps <= 0) {
    next.point = partner;
    return next;
  }
  next.point.x += (partner.x - flight.point.x) / steps;
  next.point.z += (partner.z - flight.point.z) / steps;
  return next;
}

std::optional<std::int32_t> flightFrameOf(const ThrownItemState& thrown,
                                          const ItemThrowRules& rules) {
  // main.c: tickThrownItem runs from the frame after the throw started the player's animation
  // (startAnimation(ENTITY_TABLE[0], 6)), which has gone one frame on by then: on the k-th frame
  // after the throw it shows frame k + 1. The item stays in the hand until it shows frame
  // releaseFrame.
  const std::int32_t frame = thrown.frames + 1 - rules.releaseFrame;
  if (frame < 0) {
    return std::nullopt;
  }
  return frame;
}

void restorePartner(Fighter& partner, std::int32_t hp, std::int32_t mp) {
  if (partner.fainted) {
    return;
  }
  partner.hp = std::min(partner.hp + hp, partner.stats.maxHp);
  partner.mp = std::min(partner.mp + mp, partner.stats.maxMp);
}

void revivePartner(BattleContext& context, std::int32_t hp, FighterId id) {
  BattleState& state = *context.state;
  Fighter& partner = state.fighters.at(id);
  // battle_setup.c: BTL_isBattleFinished, at the end of the revive animation (its length is not
  // modelled).
  partner.fainted = false;
  partner.struck = false;
  partner.hp = std::clamp(hp, 1, partner.stats.maxHp);
  partner.pendingDamage = 0;
  standIdle(partner);
  if (AllyState* ally = state.allyOf(id)) {
    ally->deathFrames = 0;
  } else {
    state.deathFrames = 0;
  }
  // battle_setup.c: BTL_isBattleFinished: a finisher frozen by the fall ends with its aura.
  if (state.finisher.fighter) {
    removeFinisherAura(context);
  }
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
  emitPartnerEvent(context, EventKind::Revived, partner.hp, id);
}

void throwItem(BattleContext& context, const ThrownItem& item, FighterId thrower,
               FighterId target) {
  BattleState& state = *context.state;
  const ThrownItemState thrown{.item = item, .frames = 0, .target = target};
  if (AllyState* ally = state.allyOf(thrower)) {
    ally->bagOpen = false;
    ally->thrownItem = thrown;
  } else {
    state.bagOpen = false;
    state.thrownItem = thrown;
  }
  emitPartnerEvent(context, EventKind::ItemThrown, 0, target);
}

void tickThrownItem(BattleContext& context) {
  tickFlight(context, context.state->thrownItem);
  for (AllyState& ally : context.state->allies) {
    tickFlight(context, ally.thrownItem);
  }
}

bool itemFlyingTo(const BattleState& state, FighterId id) {
  if (state.thrownItem && state.thrownItem->target == id) {
    return true;
  }
  return std::ranges::any_of(state.allies, [id](const AllyState& ally) {
    return ally.thrownItem && ally.thrownItem->target == id;
  });
}

void tickHpBars(BattleContext& context) {
  const std::int32_t shownFrames = context.rules->hud.enemyHpBarFrames;
  for (const BattleEvent& event : *context.events) {
    const bool damaged = event.kind == EventKind::AttackHit ||
                         event.kind == EventKind::AttackBlocked ||
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
