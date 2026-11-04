#pragma once

#include <dwe/game/battle/strategies.hpp>
#include <dwe/ruleset/random.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace dwe::game::battle {

// The MP a move costs the partner (battle_main.c: BTL_isMoveUsable checks one figure,
// BTL_setupMoveExecution pays another): the brain discount and the sick or injured surcharge, in
// the order each one applies them.
enum class MpCostUse : std::uint8_t { Check, Payment };
std::int32_t partnerMpCost(const BattleRules& rules, std::int32_t cost, std::int32_t brain,
                           bool sickOrInjured, MpCostUse use);

// Whether `fighter` can use the move in `slot` now (battle_main.c: BTL_isMoveUsable): there is one,
// it is not the counter or a finisher, a support move has uses left, a move that needs a target has
// one, and the MP is enough.
bool moveUsable(const BattleView& view, FighterId fighter, std::size_t slot);

// moveUsable for every equipped slot (battle_main.c: BTL_getUsableMoves).
std::array<bool, EquippedSlots> usableSlots(const BattleView& view, FighterId fighter);

// Whether any slot is usable (battle_main.c: BTL_hasAffordableMoves, which BTL_tickPartnerAI asks
// over the same marks before the partner picks a move).
bool anyUsable(const std::array<bool, EquippedSlots>& usable);

// One of the marked slots at random (battle_main.c: BTL_getRandomUsableMove): the marked ones in
// slot order, then one randomLimit over how many there are. Nothing, and no roll, when none is
// marked (the original would read past its list).
std::optional<std::size_t> randomUsableSlot(ruleset::Random& random,
                                            const std::array<bool, EquippedSlots>& usable);

} // namespace dwe::game::battle
