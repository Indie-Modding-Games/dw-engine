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
bool hasStackAt(const Inventory& inventory, std::int32_t item, bool full) {
    const std::int32_t limit = inventory.rules().bag.maxStack;
    return std::ranges::any_of(inventory.slots(), [&](const std::optional<Stack>& slot) {
        return slot && slot->item == item && (slot->count == limit) == full;
    });
}

// script_common.c: shopFillBuyItemList: affordable at the full price, and with an empty slot and no full stack of it,
// or with no empty slot and a stack of it that is not full.
bool isBuyable(const Inventory& inventory, const assets::ItemEntry& item) {
    if (item.value > inventory.money()) {
        return false;
    }
    if (hasEmptySlot(inventory)) {
        return !hasStackAt(inventory, item.index, true);
    }
    return hasStackAt(inventory, item.index, false);
}

bool inStock(const Inventory& inventory, std::int32_t item, const StockTriggers& triggers) {
    return triggers.isSet(inventory.rules().shop.stockTriggerBase + item);
}

} // namespace

std::vector<ShopOffer> shopStock(const Inventory& inventory, const assets::ItemTable& items,
                                 const StockTriggers& triggers) {
    std::vector<ShopOffer> offers;
    for (const assets::ItemEntry& item : items.items) {
        if (inStock(inventory, item.index, triggers)) {
            offers.push_back({.item = item.index, .buyable = isBuyable(inventory, item)});
        }
    }
    std::ranges::sort(offers, {}, &ShopOffer::item);
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

std::int32_t maxPurchase(const Inventory& inventory, const assets::ItemEntry& item, Buyer buyer) {
    const InventoryRules& rules = inventory.rules();
    std::int32_t most = std::min(rules.shop.maxAmount, rules.bag.maxStack - inventory.countOf(item.index));
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

core::Result<Purchase> buy(Inventory& inventory, const assets::ItemTable& items, const assets::ItemEntry& item,
                           std::int32_t amount, Buyer buyer, const StockTriggers& triggers) {
    const std::vector<ShopOffer> stock = shopStock(inventory, items, triggers);
    const auto offer = std::ranges::find(stock, item.index, &ShopOffer::item);
    if (offer == stock.end() || !offer->buyable) {
        return core::fail(messages::formatMessage(messages::NotInStock, item.index));
    }
    const std::int32_t most = maxPurchase(inventory, item, buyer);
    if (amount < 1 || amount > most) {
        return core::fail(messages::formatMessage(messages::TooMany, most, item.index));
    }
    const std::int32_t unitPrice = buyPrice(inventory, item, buyer);
    if (!canAfford(inventory, item, amount, buyer)) {
        return core::fail(
            messages::formatMessage(messages::CannotAfford, amount, item.index, amount * unitPrice, inventory.money()));
    }
    inventory.spendMoney(amount * unitPrice);
    return Purchase{.paid = amount * unitPrice,
                    .saved = amount * (item.value - unitPrice),
                    .added = inventory.add(item.index, amount),
                    .keyItem = isKeyItem(item, inventory.rules().use)};
}

std::vector<SaleOffer> saleOffers(const Inventory& inventory, const assets::ItemTable& items) {
    std::vector<SaleOffer> offers;
    for (const std::optional<Stack>& slot : inventory.slots()) {
        if (!slot) {
            continue;
        }
        const assets::ItemEntry* entry = items.findByIndex(slot->item);
        offers.push_back({.item = slot->item,
                          .count = slot->count,
                          .sellable = entry != nullptr && !isKeyItem(*entry, inventory.rules().use)});
    }
    return offers;
}

core::Result<std::int32_t> sell(Inventory& inventory, const assets::ItemEntry& item, std::int32_t amount) {
    if (isKeyItem(item, inventory.rules().use)) {
        return core::fail(messages::formatMessage(messages::NotSellable, item.index));
    }
    const std::int32_t held = inventory.countOf(item.index);
    if (amount < 1 || amount > held) {
        return core::fail(messages::formatMessage(messages::NotEnoughToSell, held, item.index, amount));
    }
    const std::int32_t received = amount * sellPrice(inventory, item);
    inventory.addMoney(received);
    inventory.remove(item.index, amount);
    return received;
}

} // namespace dwe::game::inventory
