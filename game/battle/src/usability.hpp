#pragma once

#include <dwe/game/battle/strategies.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace dwe::game::battle {

// The MP a move costs the partner (battle_main.c: BTL_isMoveUsable checks one figure, BTL_setupMoveExecution pays
// another): the brain discount and the sick or injured surcharge, in the order each one applies them.
enum class MpCostUse : std::uint8_t { Check, Payment };
std::int32_t partnerMpCost(const BattleRules& rules, std::int32_t cost, std::int32_t brain, bool sickOrInjured,
                           MpCostUse use);

// Whether `fighter` can use the move in `slot` now (battle_main.c: BTL_isMoveUsable): there is one, it is not the
// counter or a finisher, a support move has uses left, a move that needs a target has one, and the MP is enough.
bool moveUsable(const BattleView& view, FighterId fighter, std::size_t slot);

// moveUsable for every equipped slot (battle_main.c: BTL_getUsableMoves).
std::array<bool, EquippedSlots> usableSlots(const BattleView& view, FighterId fighter);

// Whether any slot is usable.
bool anyUsable(const std::array<bool, EquippedSlots>& usable);

} // namespace dwe::game::battle
