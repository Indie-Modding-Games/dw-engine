#pragma once

#include <vector>

#include "core/battle_context.hpp"

// The techniques as Folder plays them (combat-design.md §2.8, §3.10, §7.5, costo-mp.md): whom
// they reach and what they cost.
namespace dwe::game::battle {

// §2.8, §7.5: an area technique reaches whoever stands in its area on the user's opposite side,
// and its own side too when it has friendly fire; any other one its target alone.
[[nodiscard]] std::vector<FighterId> folderReached(const BattleContext& context, FighterId id,
                                                   MoveRef move, std::vector<FighterId> shared);
// §2.8: what an effect touches on its way: the user's opposite side, its own side only with
// friendly fire; an area technique touches no one beyond its area.
[[nodiscard]] bool folderTouches(const BattleContext& context, FighterId attacker, FighterId other,
                                 MoveRef move);

} // namespace dwe::game::battle
