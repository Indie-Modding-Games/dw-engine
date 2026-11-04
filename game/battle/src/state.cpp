#include <dwe/game/battle/state.hpp>

namespace dwe::game::battle {

std::vector<FighterId> BattleState::remainingOthers(FighterId self) const {
  std::vector<FighterId> others;
  for (FighterId id = 0; id < fighters.size(); ++id) {
    if (id != self && !fighters[id].defeated()) {
      others.push_back(id);
    }
  }
  return others;
}

// None left is the end of a match too (std_main.c: STD_areAllEnemyDigimonDead: HP less the damage
// on its way, 0 or under, for every rival).
std::int32_t BattleState::livingEnemies() const {
  std::int32_t count = 0;
  for (FighterId id = 1; id < fighters.size(); ++id) {
    if (!fighters[id].defeated()) {
      ++count;
    }
  }
  return count;
}

std::int64_t distanceSquared(Point a, Point b) {
  const auto dx = static_cast<std::int64_t>(a.x) - b.x;
  const auto dz = static_cast<std::int64_t>(a.z) - b.z;
  return (dx * dx) + (dz * dz);
}

} // namespace dwe::game::battle
