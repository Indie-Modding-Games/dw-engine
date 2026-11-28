#include "folder/techniques/techniques.hpp"

#include <dwe/game/battle/folder/formulas.hpp>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "folder/fighters.hpp"
#include "folder/folder.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;

// §2.8: a technique hits the user's opposite side; its own side only with friendly fire.
bool hittable(const BattleContext& context, FighterId attacker, FighterId other,
              const folder::Technique& technique) {
  const BattleState& state = *context.state;
  if (other == attacker || other >= state.fighters.size() || !standing(context, other)) {
    return false;
  }
  return state.isPlayerSide(attacker) != state.isPlayerSide(other) || technique.friendlyFire;
}

bool spreads(const BattleMove& move, const folder::Technique& technique) {
  return technique.area != AreaShape::Single && move.range != MoveRange::Self && !move.buff;
}

} // namespace

std::int32_t folderMpCost(const BattleRules& rules, const MoveCatalog& moves, MoveRef move,
                          const PartnerCondition& condition) {
  const std::int32_t cost = folder::techniqueOf(*rules.folder, moves, move).mpCost;
  const std::int32_t extra =
      condition.sick || condition.injured ? rules.condition.mpCostExtraPercent : 0;
  return cost + (cost * extra / Hundred);
}

std::vector<FighterId> folderReached(const BattleContext& context, FighterId id, MoveRef move,
                                     std::vector<FighterId> shared) {
  const folder::Technique technique =
      folder::techniqueOf(folderRules(context), *context.moves, move);
  if (!spreads(context.moves->at(move), technique)) {
    return shared;
  }
  const Fighter& self = context.fighter(id);
  Point origin = self.position;
  if (technique.area == AreaShape::AroundTarget && self.attack && self.attack->target) {
    origin = context.fighter(*self.attack->target).position;
  }
  std::vector<FighterId> reached;
  for (FighterId other = 0; other < context.state->fighters.size(); ++other) {
    if (!hittable(context, id, other, technique)) {
      continue;
    }
    const Fighter& fighter = context.fighter(other);
    const std::int64_t reach =
        static_cast<std::int64_t>(technique.areaPixels) + fighter.combatant.radius;
    if (technique.area == AreaShape::Global ||
        distanceSquared(origin, fighter.position) <= reach * reach) {
      reached.push_back(other);
    }
  }
  return reached;
}

bool folderTouches(const BattleContext& context, FighterId attacker, FighterId other,
                   MoveRef move) {
  const folder::Technique technique =
      folder::techniqueOf(folderRules(context), *context.moves, move);
  return !spreads(context.moves->at(move), technique) &&
         hittable(context, attacker, other, technique);
}

} // namespace dwe::game::battle
