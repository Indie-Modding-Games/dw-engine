#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/battle_context.hpp"

namespace dwe::game::battle {

// The statuses (battle_main.c: BTL_applyMoveStatus, BTL_tickStatusEffects,
// BTL_healStatusEffect).
void applyMoveStatus(BattleContext& context, FighterId id, MoveRef move);
void tickStatusEffects(BattleContext& context);
void cureStatus(BattleContext& context, FighterId id);
// battle_main.c: BTL_removeStatusEffects: ends the visual of the fighter's statuses, before they
// are cleared (faint, defeat, the flattening).
void removeStatusVisuals(BattleContext& context, FighterId id);
// battle_main.c: BTL_resetFlatten: back to its shape at once, the flattening and its timer gone.
// The look follows from those (updateFlatLook): the paper sprite goes (flatSprite -1) and the
// body's scale is the timer's 0, its full size.
void resetFlatten(Fighter& fighter);
// battle_effect.c: BTL_removeFinisherAura(BTL_FINISHER_AURA_ID), when a finisher is lost.
void removeFinisherAura(BattleContext& context);

} // namespace dwe::game::battle
