#pragma once

#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/rules.hpp>
#include <dwe/ruleset/random.hpp>

#include <span>
#include <string>
#include <vector>

namespace dwe::game::battle {

// What decides whether the other enemies on screen join a battle (btl.c: handleBattleStart).
struct JoinContext {
  DigimonType partnerType = DigimonType::None;
  // The item ids the tamer carries: the bell and the repel of the battle section change the roll.
  std::span<const std::string> inventory;
  // A battle of the story.
  bool scripted = false;
};

// For each candidate (the other enemies the session found on screen and in reach, nearest first),
// whether it joins the battle the touched enemy started. Stops at limits.max_enemies counting the
// touched one.
std::vector<bool> rollJoiningEnemies(const BattleRules& rules, const JoinContext& context,
                                     std::span<const DigimonType> candidates,
                                     ruleset::Random& random);

} // namespace dwe::game::battle
