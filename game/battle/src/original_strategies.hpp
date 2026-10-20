#pragma once

#include <dwe/game/battle/strategies.hpp>

#include <memory>

namespace dwe::game::battle {

// The name every registry gives the original game's behaviour.
inline constexpr std::string_view OriginalStrategy = "original";

std::unique_ptr<PartnerAi> makeOriginalPartnerAi();
std::unique_ptr<EnemyAi> makeOriginalEnemyAi();
std::unique_ptr<DamageFormula> makeOriginalDamage();
std::unique_ptr<Obedience> makeOriginalObedience();

} // namespace dwe::game::battle
