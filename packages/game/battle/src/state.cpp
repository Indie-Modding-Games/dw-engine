#include <dwe/game/battle/state.hpp>

#include <algorithm>

namespace dwe::game::battle {

std::vector<FighterId> BattleState::remainingOthers(FighterId self) const {
  std::vector<FighterId> others;
  for (FighterId id = 0; id < fighters.size(); ++id) {
    if (foes(self, id) && !fighters[id].defeated()) {
      others.push_back(id);
    }
  }
  return others;
}

// None left is the end of a match too (std_main.c: STD_areAllEnemyDigimonDead: HP less the damage
// on its way, 0 or under, for every rival).
std::int32_t BattleState::livingEnemies() const {
  std::int32_t count = 0;
  for (FighterId id = 1; id <= enemies && id < fighters.size(); ++id) {
    if (!fighters[id].defeated()) {
      ++count;
    }
  }
  return count;
}

AllyState* BattleState::allyOf(FighterId id) {
  const auto found = std::ranges::find(allies, id, &AllyState::id);
  return found != allies.end() ? &*found : nullptr;
}

const AllyState* BattleState::allyOf(FighterId id) const {
  const auto found = std::ranges::find(allies, id, &AllyState::id);
  return found != allies.end() ? &*found : nullptr;
}

std::vector<FighterId> BattleState::partnersIn() const {
  std::vector<FighterId> partners;
  if (!fighters.empty() && !fighters.front().gone()) {
    partners.push_back(PartnerId);
  }
  for (const AllyState& ally : allies) {
    if (!fighters.at(ally.id).gone()) {
      partners.push_back(ally.id);
    }
  }
  return partners;
}

BattleCounters* BattleState::countersOf(FighterId id) {
  if (id == PartnerId) {
    return &counters;
  }
  AllyState* ally = allyOf(id);
  return ally != nullptr ? &ally->counters : nullptr;
}

CommandState& BattleState::commandsOf(FighterId id) {
  if (id == PartnerId) {
    return commands;
  }
  if (AllyState* ally = allyOf(id)) {
    return ally->commands;
  }
  return rival->commands;
}

const CommandState& BattleState::commandsOf(FighterId id) const {
  if (id == PartnerId) {
    return commands;
  }
  if (const AllyState* ally = allyOf(id)) {
    return ally->commands;
  }
  return rival->commands;
}

std::optional<std::int32_t>& BattleState::fleeFramesOf(FighterId id) {
  AllyState* ally = allyOf(id);
  return ally != nullptr ? ally->fleeFrames : fleeFrames;
}

const std::optional<std::int32_t>& BattleState::fleeFramesOf(FighterId id) const {
  const AllyState* ally = allyOf(id);
  return ally != nullptr ? ally->fleeFrames : fleeFrames;
}

Point BattleState::tamerOf(FighterId id) const {
  const AllyState* ally = allyOf(id);
  return ally != nullptr ? ally->tamer : tamer;
}

std::int64_t distanceSquared(Point a, Point b) {
  const auto dx = static_cast<std::int64_t>(a.x) - b.x;
  const auto dz = static_cast<std::int64_t>(a.z) - b.z;
  return (dx * dx) + (dz * dz);
}

} // namespace dwe::game::battle
