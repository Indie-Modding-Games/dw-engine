#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/game/inventory/cards.hpp>
#include <dwe/game/inventory/inventory.hpp>

#include <cstdint>
#include <optional>
#include <vector>

// The merit exchange (shop/script_menu.c: openMeritShop, shopFillMeritItemList,
// createMeritCardTradeDialogue; script_common.c: readSelectedItemMerit): cards traded for merit
// points and items bought with them. The merit of a card is data/cards' merit_value, the price of
// an item data/items' merit_value; an item without one is not in the exchange.
namespace dwe::game::inventory {

// A row of the exchange's item list: the item, its price in merit points and whether the player
// can take it now (calculateItemMenuStrings: one that cannot shows in colour 3).
struct MeritOffer {
  assets::ItemHandle item;
  std::int32_t price = 0;
  bool buyable = false;
};

// shopFillMeritItemList: every item with a merit price, in item number order. One is buyable when
// the merit reaches its price and the bag has a free slot or a stack of it below the most.
[[nodiscard]] std::vector<MeritOffer>
meritOffers(const Inventory& inventory, const assets::ItemTable& items, std::int32_t merit);

// createMeritCardTradeDialogue: what the exchange gives for a copy of the card; nothing for a card
// the table does not have or one worth nothing.
[[nodiscard]] std::optional<std::int32_t> cardMerit(const CardCollection& cards, std::int32_t card);

// openMeritShop state 8: the merit after a trade, kept at the most (9999 in the original).
[[nodiscard]] std::int32_t addMerit(std::int32_t merit, std::int32_t gained, std::int32_t most);

} // namespace dwe::game::inventory
