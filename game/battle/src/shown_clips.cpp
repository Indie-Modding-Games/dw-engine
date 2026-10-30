#include <dwe/core/log.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

#include "battle_context.hpp"

namespace dwe::game::battle {

namespace {

// Weak below a fifth of the HP (btl.c: startBattleIdleAnimation, battle_main.c:
// BTL_startWalkingAnimation).
constexpr std::int32_t WeakHpDivisor = 5;

bool weak(const Fighter& fighter) {
  return fighter.status.poisoned || fighter.hp <= fighter.stats.maxHp / WeakHpDivisor;
}

bool happened(const BattleEvents& events, EventKind kind, FighterId id) {
  return std::ranges::any_of(
      events, [&](const BattleEvent& event) { return event.kind == kind && event.fighter == id; });
}

// anim.c: startAnimation: the clip plays from its first frame, its root step free (animFlag = 1)
// and the location's fraction dropped (locX = location << 15).
void start(Fighter& fighter, FighterId id, std::int32_t clip, std::string_view origin) {
  fighter.shown.clip = clip;
  ++fighter.shown.starts;
  fighter.shown.frame = 0;
  fighter.rootHeld = false;
  fighter.rootRemainder = {};
  core::logDebug("[port:battle] {}: fighter {} startAnimation({:#x}) for pose {}", origin, id, clip,
                 static_cast<int>(fighter.pose));
}

void startMove(const BattleContext& context, Fighter& fighter, FighterId id,
               std::optional<MoveRef> move, std::string_view origin) {
  if (const std::optional<std::int32_t> clip = moveClipOf(context, fighter, move)) {
    start(fighter, id, *clip, origin);
  }
}

bool showsAnyOf(const Fighter& fighter, std::int32_t a, std::int32_t b) {
  return fighter.shown.clip == a || fighter.shown.clip == b;
}

void updateShownClip(const BattleContext& context, FighterId id) {
  Fighter& fighter = context.fighter(id);
  ShownClip& shown = fighter.shown;
  const bool entered = shown.pose != fighter.pose;
  shown.pose = fighter.pose;
  shown.frozen = false;
  const BattleEvents& events = *context.events;
  switch (fighter.pose) {
  case Pose::Standing:
    // btl.c: handleBattleIdle keeps either stance it shows.
    if (!showsAnyOf(fighter, StanceClip, WeakStanceClip)) {
      start(fighter, id, stanceClipOf(fighter), "handleBattleIdle (btl.c:161)");
    }
    break;
  case Pose::Walking:
    if (!showsAnyOf(fighter, WalkClip, WeakWalkClip)) {
      start(fighter, id, weak(fighter) ? WeakWalkClip : WalkClip,
            "BTL_setWalking (battle_main.c:1381)");
    }
    break;
  case Pose::Attacking:
    if (entered || happened(events, EventKind::AttackStarted, id)) {
      startMove(context, fighter, id,
                fighter.attack ? std::optional{fighter.attack->move} : std::nullopt,
                "BTL_startQueuedMove (battle_main.c:2473)");
    }
    break;
  case Pose::Charging:
    // The finisher's clip stands on its first frame while the aura plays.
    if (entered) {
      startMove(context, fighter, id, fighter.queuedMove,
                "BTL_startQueuedMove (battle_main.c:2445)");
    }
    shown.frozen = true;
    break;
  case Pose::HitReaction:
    if (entered || happened(events, EventKind::AttackHit, id)) {
      start(fighter, id, fighter.hitFromBehind ? HitFromBehindClip : HitClip,
            "BTL_handleHitReaction (battle_main.c:3093)");
    }
    // BTL_tickHitState lets the clip run once the invulnerability is over.
    shown.frozen = fighter.invulnerableFrames > 0;
    break;
  case Pose::Blocking:
    if (entered || happened(events, EventKind::AttackBlocked, id)) {
      start(fighter, id, BlockClip, "BTL_tickAttackHits (battle_main.c:2220)");
    }
    shown.frozen = fighter.invulnerableFrames > 0;
    break;
  case Pose::Stunned:
    if (shown.clip != WeakStanceClip) {
      start(fighter, id, WeakStanceClip, "BTL_tickStunState (battle_main.c:1018)");
    }
    shown.frozen = true;
    break;
  case Pose::Squashed:
    if (entered) {
      start(fighter, id, WeakStanceClip, "BTL_tickStatusEffects (battle_main.c:2838)");
    }
    break;
  case Pose::Disobeying:
    if (shown.clip != DisobeyClip) {
      start(fighter, id, DisobeyClip, "BTL_tickSenileState (battle_main.c:1078)");
    }
    break;
  case Pose::Fainting:
  case Pose::Fainted:
    // The faint clip plays once and holds its last frame.
    if (shown.clip != FaintClip) {
      start(fighter, id, FaintClip, "battle_main.c:1651 (the faint)");
    }
    break;
  }
}

} // namespace

// battle_main.c: BTL_setupQueuedMove: queuedAnim is the species' animation of the move, the first
// move animation plus the move's place in the species' list. Nothing for a move outside the list
// (the flattened fighter's shot), which keeps the clip it shows.
std::optional<std::int32_t> moveClipOf(const BattleContext& context, const Fighter& fighter,
                                       std::optional<MoveRef> move) {
  if (!move) {
    return std::nullopt;
  }
  const auto& list = fighter.combatant.speciesMoves;
  const auto found = std::ranges::find(list, std::optional{context.moves->at(*move).id});
  if (found == list.end()) {
    return std::nullopt;
  }
  return FirstMoveAnimation + static_cast<std::int32_t>(std::distance(list.begin(), found));
}

std::optional<std::int32_t> clipLength(const Fighter& fighter, std::int32_t clip) {
  if (!fighter.clips) {
    return std::nullopt;
  }
  const auto found = fighter.clips->find(clip);
  if (found == fighter.clips->end() || found->second.repeatFrom) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(found->second.length());
}

std::optional<bool> clipEnded(const Fighter& fighter, std::int32_t clip) {
  if (!clipLength(fighter, clip)) {
    return std::nullopt;
  }
  return fighter.shown.clip == clip && fighter.clips->at(clip).ended(fighter.shown.frame);
}

std::optional<RootStep> nextRootStep(const Fighter& fighter) {
  // A frozen clip is tested too: the frame's step test runs before the pose lets it go on.
  if (!fighter.clips) {
    return std::nullopt;
  }
  const auto found = fighter.clips->find(fighter.shown.clip);
  if (found == fighter.clips->end()) {
    return std::nullopt;
  }
  return found->second.stepAt(fighter.shown.frame);
}

std::int32_t stanceClipOf(const Fighter& fighter) {
  return weak(fighter) ? WeakStanceClip : StanceClip;
}

void startShownClips(BattleState& state) {
  for (Fighter& fighter : state.fighters) {
    fighter.shown = {
        .clip = stanceClipOf(fighter), .starts = 1, .frozen = false, .pose = Pose::Standing};
  }
}

void updateShownClips(const BattleContext& context) {
  for (FighterId id = 0; id < context.state->fighters.size(); ++id) {
    updateShownClip(context, id);
  }
}

} // namespace dwe::game::battle
