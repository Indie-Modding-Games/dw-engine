#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/inventory/inventory.hpp>

#include <cstdint>
#include <vector>

// The shops' trade (script_common.c). The menus are the UI's; the stock comes from the story's triggers.
namespace dwe::game::inventory {

// The story's triggers, which the session gives the shop: item i is in stock while trigger stock_trigger_base + i is
// set.
class StockTriggers {
public:
    StockTriggers() = default;
    virtual ~StockTriggers() = default;
    StockTriggers(const StockTriggers&) = delete;
    StockTriggers& operator=(const StockTriggers&) = delete;
    StockTriggers(StockTriggers&&) = delete;
    StockTriggers& operator=(StockTriggers&&) = delete;

    [[nodiscard]] virtual bool isSet(std::int32_t trigger) const = 0;
};

// Who buys: a baby partner gets the discount (script_common.c: isPartnerBaby).
struct Buyer {
    bool partnerIsBaby = false;
};

struct ShopOffer {
    assets::ItemHandle item;
    // Whether it can be bought now: affordable and with room in the bag.
    bool buyable = false;
};

struct SaleOffer {
    assets::ItemHandle item;
    std::int32_t count = 0;
    // Only items that can be thrown away are sold.
    bool sellable = false;
};

struct Purchase {
    std::int32_t paid = 0;
    // What the baby discount saved.
    std::int32_t saved = 0;
    AddResult added{};
    // Whether the item bought is a key item (isKeyItem), for the session's autosave.
    bool keyItem = false;
};

// The items in stock, in item number order, each marked buyable or not (script_common.c: shopFillBuyItemList). Only
// the original's items have a stock trigger: an item a pack adds is never in stock.
std::vector<ShopOffer> shopStock(const Inventory& inventory, const assets::ItemTable& items,
                                 const StockTriggers& triggers);

// The price of one item to this buyer.
std::int32_t buyPrice(const Inventory& inventory, const assets::ItemEntry& item, Buyer buyer);

// The price the shop pays for one item.
std::int32_t sellPrice(const Inventory& inventory, const assets::ItemEntry& item);

// The most of an item the buyer can buy at once (script_common.c: createItemMenuAmountBox).
std::int32_t maxPurchase(const Inventory& inventory, const assets::ItemTable& items, assets::ItemHandle handle,
                         Buyer buyer);

// Whether the money covers `amount` of the item.
bool canAfford(const Inventory& inventory, const assets::ItemEntry& item, std::int32_t amount, Buyer buyer);

// Buys `amount` of an item in stock (script_common.c: tickItemMenuAmountBox).
core::Result<Purchase> buy(Inventory& inventory, const assets::ItemTable& items, assets::ItemHandle handle,
                           std::int32_t amount, Buyer buyer, const StockTriggers& triggers);

// The bag's stacks as the shop's sell list shows them, in bag order (script_common.c: shopFillSellItemList).
std::vector<SaleOffer> saleOffers(const Inventory& inventory, const assets::ItemTable& items);

// Sells `amount` of an item and returns the money received.
core::Result<std::int32_t> sell(Inventory& inventory, const assets::ItemTable& items, assets::ItemHandle handle,
                                std::int32_t amount);

} // namespace dwe::game::inventory
