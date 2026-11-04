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

void emitPartnerEvent(BattleContext& context, EventKind kind, std::int32_t amount) {
  context.emit({.kind = kind,
                .fighter = PartnerId,
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
void boost(BattleContext& context, Fighter& partner, const StatBoost& disk) {
  const auto raise = [&context, &disk](std::int32_t& stat, std::int32_t start, std::int32_t amount,
                                       EventKind shown) {
    if (amount == 0) {
      return;
    }
    std::int32_t gain = amount;
    if (!disk.ignoresLimit) {
      const std::int32_t limit = std::min(start * disk.limitPercent / WholePercent, disk.limitCap);
      gain = std::min(amount, limit - stat);
    }
    stat = std::min(stat + gain, disk.limitCap);
    emitPartnerEvent(context, shown, gain);
  };
  raise(partner.stats.offense, partner.startStats.offense, disk.offense, EventKind::OffenseRaised);
  raise(partner.stats.defense, partner.startStats.defense, disk.defense, EventKind::DefenseRaised);
  raise(partner.stats.speed, partner.startStats.speed, disk.speed, EventKind::SpeedRaised);
}

// battle_main.c: BTL_healStatusEffect(1): the rings, in the colour of the last of poison,
// confusion, stun and flattening the partner has, when it has any.
void showCureRings(BattleContext& context, const Fighter& partner) {
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
    context.visuals->addBuffRings(PartnerId, partner, *kind);
  }
}

// item.c: the item's handler, now that it reached the partner. The healing ones show what they
// heal over the partner (addEntityText(partner, 0, 0xb, value, icon)): the item's own amount, not
// what the maximum let in; handleDoubleFloppy shows the HP and then the MP.
void land(BattleContext& context, const ThrownItem& item) {
  Fighter& partner = context.fighter(PartnerId);
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
      revivePartner(context, hp);
    } else {
      restorePartner(partner, hp, 0);
    }
    emitPartnerEvent(context, EventKind::HpRestored, hp);
  }
  restorePartner(partner, item.hp, item.mp);
  if (item.hp > 0) {
    emitPartnerEvent(context, EventKind::HpRestored, item.hp);
  }
  if (item.mp > 0) {
    emitPartnerEvent(context, EventKind::MpRestored, item.mp);
  }
  if (item.curesStatus) {
    if (item.cureRings && context.visuals != nullptr) {
      showCureRings(context, partner);
    }
    cureStatus(context, PartnerId);
  }
  if (item.protects) {
    partner.status.protectedFromStatus = true;
  }
  // item.c: handleHPHealingItem, handleMPHealingItem, handleDoubleFloppy and handleRestore end
  // with addHealingParticleEffect(partner, ring).
  if (item.healingParticles) {
    emitPartnerEvent(context, EventKind::HealingShown, item.healingRing ? 1 : 0);
  }
  if (item.boost) {
    boost(context, partner, *item.boost);
    // main.c: handleBuffDisks ends with BTL_addBuffDiskEffect, unless the partner is down.
    if (!partner.fainted && context.visuals != nullptr) {
      context.visuals->addBuffDisk(PartnerId, partner);
    }
  }
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

void revivePartner(BattleContext& context, std::int32_t hp) {
  BattleState& state = *context.state;
  Fighter& partner = state.partner();
  // battle_setup.c: BTL_isBattleFinished, at the end of the revive animation (its length is not
  // modelled).
  partner.fainted = false;
  partner.struck = false;
  partner.hp = std::clamp(hp, 1, partner.stats.maxHp);
  partner.pendingDamage = 0;
  standIdle(partner);
  state.deathFrames = 0;
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
  ++thrown->frames;
  // The step of the flight's last frame lands it (TAMER_ITEM.time reaches 0x18 after it).
  const std::optional<std::int32_t> flight = flightFrameOf(*thrown, rules);
  if (!flight || *flight < rules.flightFrames - 1) {
    return;
  }
  const ThrownItem item = thrown->item;
  thrown.reset();
  land(context, item);
  // main.c: tickThrownItem: BTL_addItemParticles(ENTITY_TABLE[1]) after the item's handler.
  if (context.visuals != nullptr) {
    context.visuals->addItemParticles(PartnerId, context.fighter(PartnerId));
  }
  emitPartnerEvent(context, EventKind::ItemLanded, 0);
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
