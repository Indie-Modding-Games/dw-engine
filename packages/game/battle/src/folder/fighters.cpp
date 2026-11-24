#include "folder/fighters.hpp"

#include <dwe/game/battle/folder/formulas.hpp>

#include <cstdint>
#include <optional>

namespace dwe::game::battle {

const FolderRules& folderRules(const BattleContext& context) {
  return *context.rules->folder;
}

FolderFighter& folderFighter(const BattleContext& context, FighterId id) {
  return context.state->folder->fighters.at(id);
}

FighterStats folderStats(const BattleContext& context, FighterId id) {
  FighterStats stats = context.fighter(id).stats;
  if (folderFighter(context, id).broken()) {
    const FolderRules& rules = folderRules(context);
    stats.offense = folder::brokenStat(rules, stats.offense);
    stats.defense = folder::brokenStat(rules, stats.defense);
    stats.speed = folder::brokenStat(rules, stats.speed);
  }
  return stats;
}

bool standing(const BattleContext& context, FighterId id) {
  const Fighter& fighter = context.fighter(id);
  return !fighter.gone() && !fighter.fainted && !fighter.defeated();
}

bool validFoe(const BattleContext& context, FighterId self, std::optional<FighterId> other) {
  return other && *other < context.state->fighters.size() && context.state->foes(self, *other) &&
         standing(context, *other);
}

std::optional<FighterId> nearestFoe(const BattleContext& context, FighterId id) {
  const BattleState& state = *context.state;
  const Fighter& self = state.fighters.at(id);
  std::optional<FighterId> nearest;
  std::int64_t best = 0;
  for (FighterId other = 0; other < state.fighters.size(); ++other) {
    if (!validFoe(context, id, other)) {
      continue;
    }
    const std::int64_t distance = distanceSquared(self.position, state.fighters[other].position);
    if (!nearest || distance < best) {
      nearest = other;
      best = distance;
    }
  }
  return nearest;
}

// The foe a strip order aims at: the one Change picked, kept by the order from then on.
std::optional<FighterId> aimOf(const BattleContext& context, FighterId side) {
  const std::optional<FighterId> target = context.fighter(side).target;
  return validFoe(context, side, target) ? target : std::nullopt;
}

} // namespace dwe::game::battle
