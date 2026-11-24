#pragma once

#include <optional>

#include "core/battle_context.hpp"

// The fighters as the Folder battle sees them: their own state, their stats under a break and the
// foes they may aim at.
namespace dwe::game::battle {

[[nodiscard]] const FolderRules& folderRules(const BattleContext& context);
[[nodiscard]] FolderFighter& folderFighter(const BattleContext& context, FighterId id);
// The stats a fighter fights with now: the break halves ATK, VEL and DEF (§2.9).
[[nodiscard]] FighterStats folderStats(const BattleContext& context, FighterId id);
// On its feet: neither gone, fainted nor defeated.
[[nodiscard]] bool standing(const BattleContext& context, FighterId id);
// `other` is a standing foe of `self`.
[[nodiscard]] bool validFoe(const BattleContext& context, FighterId self,
                            std::optional<FighterId> other);
// The nearest foe of `id` still standing, if any.
[[nodiscard]] std::optional<FighterId> nearestFoe(const BattleContext& context, FighterId id);
// The foe a strip order aims at: the one Change picked, kept by the order from then on.
[[nodiscard]] std::optional<FighterId> aimOf(const BattleContext& context, FighterId side);

} // namespace dwe::game::battle
