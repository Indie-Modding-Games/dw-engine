#pragma once

#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/state.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace dwe::game::battle {

// What happened in a frame, for the UI to play animations, numbers and sounds. Each event names the
// fighters by id and, where there is one, the move by its catalog reference; the UI keys its
// animations on the kind and the move id.
enum class EventKind : std::uint8_t {
  CommandGiven,    // `command` reached the partner
  TargetChanged,   // `fighter` now aims at `other`
  Disobeyed,       // `fighter` ignores orders for a while
  MoveQueued,      // `fighter` plans `move`
  AttackStarted,   // `fighter` starts `move` against `other` (none: at the air or itself)
  AttackReleased,  // the attack of `fighter` leaves it (the projectile, the swing that lands)
  AttackHit,       // `move` of `other` hit `fighter` for `amount`
  AttackBlocked,   // `fighter` blocked `move` of `other`, taking `amount`
  AttackMissed,    // the attack of `other` reached nothing it could hurt
  Countered,       // `fighter` counters `other`
  Buffed,          // `move` raised the stats of `fighter`
  StatusApplied,   // `status` on `fighter`
  StatusCleared,   // `status` off `fighter`
  PoisonDamage,    // poison took `amount` from `fighter`
  FinisherReady,   // the gauge of `fighter` is full
  FinisherStarted, // `fighter` starts the aura of `move`
  FinisherCharged, // the partner's charge is now `amount`
  Fainted,         // `fighter` fell
  Revived,         // `fighter` got up with `amount` HP
  FleeStarted,     // the tamer calls the partner back
  ItemThrown,      // the tamer throws an item at the partner
  ItemLanded,      // the thrown item reached the partner and did its effect
  BattleEnded,     // the result is ready
};

struct BattleEvent {
  EventKind kind = EventKind::BattleEnded;
  FighterId fighter = PartnerId;
  std::optional<FighterId> other;
  std::optional<MoveRef> move;
  std::int32_t amount = 0;
  StatusEffect status = StatusEffect::None;
  Command command = Command::None;
};

using BattleEvents = std::vector<BattleEvent>;

} // namespace dwe::game::battle
