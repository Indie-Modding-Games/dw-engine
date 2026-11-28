#pragma once

#include "core/battle_context.hpp"

// How a fighter places itself between and during orders (combat-design.md §2.9).
namespace dwe::game::battle {

[[nodiscard]] Command stanceCommand(Stance stance);
void face(const BattleContext& context, FighterId id, FighterId target);
// §2.9: where the stance keeps the fighter while it has no technique to position for.
void moveByStance(BattleContext& context, FighterId id);
// §2.9, §7.4: walks until the technique reaches its target; true once it does. `pointBlank`
// (Aggressive, for `folder.chase_frames`) closes to contact first.
bool positionFor(BattleContext& context, FighterId id, FighterId target, const BattleMove& move,
                 bool pointBlank);
// Whether `move` reaches `target` now; a ranged one keeps the Classic band around its distance.
[[nodiscard]] bool inReach(const BattleContext& context, FighterId id, FighterId target,
                           const BattleMove& move);

} // namespace dwe::game::battle
