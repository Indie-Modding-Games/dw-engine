#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/battle_context.hpp"

namespace dwe::game::battle {

// The player's thrown item (main.c: startThrowingItem, tickThrownItem) and what it
// does when it lands (item.c), and the HUD's HP bars (battle_main.c: BTL_renderEnemyHPBars).
// The item `thrower`'s player throws at `target` (the partner's player for the partner).
void throwItem(BattleContext& context, const ThrownItem& item, FighterId thrower = PartnerId,
               FighterId target = PartnerId);
// Whether an item is on its way to a partner: its countdown waits meanwhile.
bool itemFlyingTo(const BattleState& state, FighterId id);
void tickThrownItem(BattleContext& context);
// The frame of the thrown item's flight, or none while it is still in the player's hand.
[[nodiscard]] std::optional<std::int32_t> flightFrameOf(const ThrownItemState& thrown,
                                                        const ItemThrowRules& rules);
// HP and MP healed, each up to its maximum; nothing while the partner is down.
void restorePartner(Fighter& partner, std::int32_t hp, std::int32_t mp);
// Gets the fallen partner up with `hp` and resets every fighter's action (battle_setup.c:
// BTL_isBattleFinished).
void revivePartner(BattleContext& context, std::int32_t hp, FighterId id = PartnerId);
// An enemy's HP bar shows again on damage and counts the frames after its hit reaction and its
// damage drain.
void tickHpBars(BattleContext& context);

} // namespace dwe::game::battle
