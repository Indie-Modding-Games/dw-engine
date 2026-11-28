#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "mechanics/decisions/decisions.hpp"

namespace dwe::game::battle {

void setTarget(BattleContext& context, FighterId id, std::optional<FighterId> target) {
  Fighter& fighter = context.fighter(id);
  if (fighter.target != target && target) {
    context.emit({.kind = EventKind::TargetChanged,
                  .fighter = id,
                  .other = target,
                  .move = std::nullopt,
                  .amount = 0,
                  .status = StatusEffect::None,
                  .command = Command::None});
  }
  fighter.target = target;
}

void selectTarget(BattleContext& context, FighterId id, std::optional<FighterId> toward,
                  bool forced) {
  // vs_combat.c: VS__tickDigimonAI: a versus side always targets the other one.
  if (context.state->versus()) {
    setTarget(context, id, id == PartnerId ? RivalId : PartnerId);
    return;
  }
  Fighter& fighter = context.fighter(id);
  const bool playerSide = context.state->isPlayerSide(id);
  if (!playerSide || forced) {
    if (!toward) {
      if (!playerSide) {
        setTarget(context, id, enemyTargetOf(context, id));
      } else if (id == PartnerId) {
        setTarget(context, id, PartnerId);
      } else {
        setTarget(context, id, context.strategies->partnerAi->chooseTarget(context.view(id)));
      }
      return;
    }
    if (!playerSide && !context.strategies->obedience->enemyTurnsToAttacker(context.view(), id)) {
      setTarget(context, id, enemyTargetOf(context, id));
      return;
    }
    if (context.fighter(*toward).defeated()) {
      return;
    }
    fighter.chasingAttacker = true;
    setTarget(context, id, toward);
    return;
  }
  setTarget(context, id, context.strategies->partnerAi->chooseTarget(context.view(id)));
}

FighterId enemyTargetOf(const BattleContext& context, FighterId enemy) {
  const BattleState& state = *context.state;
  if (state.allies.empty()) {
    return PartnerId;
  }
  const Fighter& self = state.fighters.at(enemy);
  const auto standing = [&state](FighterId id) {
    const Fighter& fighter = state.fighters.at(id);
    return !fighter.gone() && fighter.hp > 0;
  };
  if (self.target && state.isPlayerSide(*self.target) && standing(*self.target)) {
    return *self.target;
  }
  std::optional<FighterId> nearest;
  std::int64_t best = 0;
  for (const FighterId id : state.partnersIn()) {
    const std::int64_t distance = distanceSquared(self.position, state.fighters.at(id).position);
    if (standing(id) && (!nearest || distance < best)) {
      nearest = id;
      best = distance;
    }
  }
  if (nearest) {
    return *nearest;
  }
  const std::vector<FighterId> in = state.partnersIn();
  return in.empty() ? PartnerId : in.front();
}

} // namespace dwe::game::battle
