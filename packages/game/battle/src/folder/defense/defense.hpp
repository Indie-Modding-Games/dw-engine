#pragma once

#include "core/battle_context.hpp"

// The Folder battle's defense (combat-design.md §2.9, §6.2): cover, the defense bar and its break.
namespace dwe::game::battle {

void handleFolderAttack(BattleContext& context, const AttackInFlight& attack);
// The bar drains after a rest, and always during a break; the break counts down; VEL sets the
// walking speed (§6.4).
void tickDefense(BattleContext& context, FighterId id);

} // namespace dwe::game::battle
