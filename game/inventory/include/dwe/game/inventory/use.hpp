#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/inventory/item_effect.hpp>
#include <dwe/game/inventory/item_effects.hpp>

#include <cstdint>

namespace dwe::game::inventory {

// What came of using an item.
enum class UseOutcome : std::uint8_t {
  // Consumed and applied.
  Used,
  // The partner did not take it; nothing was consumed.
  Refused,
  // Its item_color does not allow it here.
  NotUsableHere,
  // The bag has none.
  NotInBag,
  // It does nothing and the quirk useless_items_consumed is off, so it was kept.
  Useless,
  // The partner takes it (offerItem): nothing was consumed yet; giveItem consumes and applies it.
  Accepted,
};

// What came of using an item, and whether it is a key item (isKeyItem), for the session's autosave.
struct UseResult {
  UseOutcome outcome = UseOutcome::NotInBag;
  bool keyItem = false;
};

// Uses one of an item on the target: checks the context against its item_color, resolves its
// effect, lets the partner refuse it in the field (partner.c: partner states 4 to 6), consumes one
// and applies it. In battle the item is consumed when thrown and always lands (main.c:
// startThrowingItem, tickThrownItem).
core::Result<UseResult> useItem(Inventory& inventory, assets::ItemHandle item, UseContext context,
                                const assets::ItemTable& items, const ItemEffects& effects,
                                ItemEffectTarget& target);

// The first half of useItem, as the player holds the item out to the partner (partner.c:
// partnerTickFeedItem sub state 4): the same checks and the partner's refusal; nothing is consumed.
// Accepted means giveItem may finish it.
core::Result<UseResult> offerItem(const Inventory& inventory, assets::ItemHandle item,
                                  UseContext context, const assets::ItemTable& items,
                                  const ItemEffects& effects, ItemEffectTarget& target);

// The second half, once the partner ate it (sub state 6): consumes one and applies it, with no
// second refusal; NotInBag when the bag has none left.
core::Result<UseResult> giveItem(Inventory& inventory, assets::ItemHandle item, UseContext context,
                                 const assets::ItemTable& items, const ItemEffects& effects,
                                 ItemEffectTarget& target);

} // namespace dwe::game::inventory
