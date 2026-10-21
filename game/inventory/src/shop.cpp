#include <dwe/game/inventory/shop.hpp>

#include <algorithm>

#include "messages.hpp"

namespace dwe::game::inventory {

namespace {

constexpr std::int32_t FullPercent = 100;

bool hasEmptySlot(const Inventory& inventory) {
    return std::ranges::any_of(inventory.slots(), [](const std::optional<Stack>& slot) { return !slot.has_value(); });
}

// Whether a stack of the item is at the limit, or not at it.
bool hasStackAt(const Inventory& inventory, assets::ItemHandle item, bool full) {
    const std::int32_t limit = inventory.rules().bag.maxStack;
    return std::ranges::any_of(inventory.slots(), [&](const std::optional<Stack>& slot) {
        return slot && slot->item == item && (slot->count == limit) == full;
    });
}

// script_common.c: shopFillBuyItemList: affordable at the full price, and with an empty slot and no full stack of it,
// or with no empty slot and a stack of it that is not full.
bool isBuyable(const Inventory& inventory, const assets::ItemTable& items, assets::ItemHandle handle) {
    const assets::ItemEntry& item = items.at(handle);
    if (item.value > inventory.money()) {
        return false;
    }
    if (hasEmptySlot(inventory)) {
        return !hasStackAt(inventory, handle, true);
    }
    return hasStackAt(inventory, handle, false);
}

// The stock flags are story triggers numbered after the original's items, so only those can be in stock.
bool inStock(const Inventory& inventory, const assets::ItemEntry& item, const StockTriggers& triggers) {
    return item.originalIndex && triggers.isSet(inventory.rules().shop.stockTriggerBase + item.index);
}

} // namespace

std::vector<ShopOffer> shopStock(const Inventory& inventory, const assets::ItemTable& items,
                                 const StockTriggers& triggers) {
    std::vector<ShopOffer> offers;
    std::vector<const assets::ItemEntry*> stocked;
    for (const assets::ItemEntry& item : items.entries()) {
        if (inStock(inventory, item, triggers)) {
            stocked.push_back(&item);
        }
    }
    std::ranges::sort(stocked, {}, &assets::ItemEntry::index);
    for (const assets::ItemEntry* item : stocked) {
        const assets::ItemHandle handle = items.handleOf(*item);
        offers.push_back({.item = handle, .buyable = isBuyable(inventory, items, handle)});
    }
    return offers;
}

std::int32_t buyPrice(const Inventory& inventory, const assets::ItemEntry& item, Buyer buyer) {
    if (!buyer.partnerIsBaby) {
        return item.value;
    }
    return item.value * inventory.rules().shop.babyDiscountPercent / FullPercent;
}

std::int32_t sellPrice(const Inventory& inventory, const assets::ItemEntry& item) {
    return item.value / inventory.rules().shop.sellDivisor;
}

std::int32_t maxPurchase(const Inventory& inventory, const assets::ItemTable& items, assets::ItemHandle handle,
                         Buyer buyer) {
    const assets::ItemEntry& item = items.at(handle);
    const InventoryRules& rules = inventory.rules();
    std::int32_t most = std::min(rules.shop.maxAmount, rules.bag.maxStack - inventory.countOf(handle));
    // The original limits by the full price even when the baby discount applies.
    const std::int32_t price =
        rules.quirks.babyDiscountLimitedByFullPrice ? item.value : buyPrice(inventory, item, buyer);
    if (price > 0 && most * price > inventory.money()) {
        most = inventory.money() / price;
    }
    return std::max(most, 0);
}

bool canAfford(const Inventory& inventory, const assets::ItemEntry& item, std::int32_t amount, Buyer buyer) {
    return amount * buyPrice(inventory, item, buyer) <= inventory.money();
}

core::Result<Purchase> buy(Inventory& inventory, const assets::ItemTable& items, assets::ItemHandle handle,
                           std::int32_t amount, Buyer buyer, const StockTriggers& triggers) {
    const assets::ItemEntry& item = items.at(handle);
    const std::vector<ShopOffer> stock = shopStock(inventory, items, triggers);
    const auto offer = std::ranges::find(stock, handle, &ShopOffer::item);
    if (offer == stock.end() || !offer->buyable) {
        return core::fail(messages::formatMessage(messages::NotInStock, item.id));
    }
    const std::int32_t most = maxPurchase(inventory, items, handle, buyer);
    if (amount < 1 || amount > most) {
        return core::fail(messages::formatMessage(messages::TooMany, most, item.id));
    }
    const std::int32_t unitPrice = buyPrice(inventory, item, buyer);
    if (!canAfford(inventory, item, amount, buyer)) {
        return core::fail(
            messages::formatMessage(messages::CannotAfford, amount, item.id, amount * unitPrice, inventory.money()));
    }
    inventory.spendMoney(amount * unitPrice);
    return Purchase{.paid = amount * unitPrice,
                    .saved = amount * (item.value - unitPrice),
                    .added = inventory.add(handle, amount),
                    .keyItem = isKeyItem(item, inventory.rules().use)};
}

std::vector<SaleOffer> saleOffers(const Inventory& inventory, const assets::ItemTable& items) {
    std::vector<SaleOffer> offers;
    for (const std::optional<Stack>& slot : inventory.slots()) {
        if (!slot) {
            continue;
        }
        offers.push_back({.item = slot->item,
                          .count = slot->count,
                          .sellable = !isKeyItem(items.at(slot->item), inventory.rules().use)});
    }
    return offers;
}

core::Result<std::int32_t> sell(Inventory& inventory, const assets::ItemTable& items, assets::ItemHandle handle,
                                std::int32_t amount) {
    const assets::ItemEntry& item = items.at(handle);
    if (isKeyItem(item, inventory.rules().use)) {
        return core::fail(messages::formatMessage(messages::NotSellable, item.id));
    }
    const std::int32_t held = inventory.countOf(handle);
    if (amount < 1 || amount > held) {
        return core::fail(messages::formatMessage(messages::NotEnoughToSell, held, item.id, amount));
    }
    const std::int32_t received = amount * sellPrice(inventory, item);
    inventory.addMoney(received);
    inventory.remove(handle, amount);
    return received;
}

} // namespace dwe::game::inventory
