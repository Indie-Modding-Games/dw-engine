#include <dwe/game/inventory/shop.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <set>

#include "fixture.hpp"

using namespace dwe::game::inventory;
using namespace dwe::game::inventory::testing;

namespace {

// The story's triggers: the items in stock.
class StockedItems final : public StockTriggers {
public:
    explicit StockedItems(std::set<std::int32_t> items) : items_(std::move(items)) {}
    [[nodiscard]] bool isSet(std::int32_t trigger) const override {
        // script_common.c: shopFillBuyItemList: trigger 0x180 + item.
        return items_.contains(trigger - 384);
    }

private:
    std::set<std::int32_t> items_;
};

constexpr Buyer Adult{.partnerIsBaby = false};
constexpr Buyer Baby{.partnerIsBaby = true};

} // namespace

TEST_CASE("the stock is the items whose trigger is set, each buyable if affordable and with room") {
    const auto items = fixtureItems();
    Inventory inventory = originalInventory();
    const StockedItems stock{{Tonic, Floppy, Steak}};
    inventory.addMoney(500);
    inventory.add(Steak, 99);
    const std::vector<ShopOffer> offers = shopStock(inventory, items, stock);
    REQUIRE(offers.size() == 3);
    CHECK(offers[0].item == Tonic);
    CHECK(offers[0].buyable);
    // Floppy costs 2000; Steak's stack is full.
    CHECK_FALSE(offers[1].buyable);
    CHECK_FALSE(offers[2].buyable);
}

TEST_CASE("with the bag full only items with a stack that is not full can be bought") {
    const auto items = fixtureItems();
    Inventory inventory = originalInventory();
    inventory.addMoney(10000);
    for (std::int32_t item = 0; item < 10; ++item) {
        inventory.add(100 + item, 1);
    }
    inventory.remove(100, 1);
    inventory.add(Tonic, 1);
    const StockedItems stock{{Tonic, Bun}};
    const std::vector<ShopOffer> offers = shopStock(inventory, items, stock);
    CHECK(offers[0].buyable);
    CHECK_FALSE(offers[1].buyable);
}

TEST_CASE("buying pays the price, a baby partner pays 90 %, and the amount is limited by stack, money and 99") {
    const auto items = fixtureItems();
    Inventory inventory = originalInventory();
    const StockedItems stock{{Steak}};
    const dwe::assets::ItemEntry& steak = *items.findByIndex(Steak);
    inventory.addMoney(1000);
    CHECK(buyPrice(inventory, steak, Adult) == 120);
    CHECK(buyPrice(inventory, steak, Baby) == 108);
    CHECK(maxPurchase(inventory, steak, Adult) == 8);
    // The original limits the amount by the full price even with the discount.
    CHECK(maxPurchase(inventory, steak, Baby) == 8);
    CHECK(canAfford(inventory, steak, 9, Baby));
    CHECK_FALSE(canAfford(inventory, steak, 9, Adult));

    const auto bought = buy(inventory, items, steak, 5, Baby, stock);
    REQUIRE_MESSAGE(bought.has_value(), errorOf(bought));
    CHECK(bought->paid == 540);
    CHECK(bought->saved == 60);
    CHECK(bought->added.added == 5);
    CHECK_FALSE(bought->keyItem);
    CHECK(inventory.money() == 460);
    CHECK(inventory.countOf(Steak) == 5);

    CHECK_FALSE(buy(inventory, items, steak, 4, Adult, stock).has_value());
    CHECK_FALSE(buy(inventory, items, *items.findByIndex(Tonic), 1, Adult, stock).has_value());
}

TEST_CASE("without the quirk a baby's discount counts for the most it can buy") {
    const auto items = fixtureItems();
    Inventory inventory = originalInventory();
    InventoryRules rules = inventory.rules();
    rules.quirks.babyDiscountLimitedByFullPrice = false;
    inventory.setRules(rules, stackingRules().create(*originalRuleset().section(SectionName), StackingPath).value());
    inventory.addMoney(1000);
    CHECK(maxPurchase(inventory, *items.findByIndex(Steak), Baby) == 9);
}

TEST_CASE("the stack's room bounds a purchase: 99 less what the bag holds") {
    const auto items = fixtureItems();
    Inventory inventory = originalInventory();
    inventory.addMoney(999999);
    inventory.add(Bun, 95);
    CHECK(maxPurchase(inventory, *items.findByIndex(Bun), Adult) == 4);
    CHECK(maxPurchase(inventory, *items.findByIndex(Tonic), Adult) == 99);
}

TEST_CASE("selling pays half the value, only for items that can be thrown away, up to the money's limit") {
    const auto items = fixtureItems();
    Inventory inventory = originalInventory();
    inventory.add(Steak, 3);
    inventory.add(Rod, 1);
    const std::vector<SaleOffer> offers = saleOffers(inventory, items);
    REQUIRE(offers.size() == 2);
    CHECK(offers[0].sellable);
    CHECK_FALSE(offers[1].sellable);

    const auto received = sell(inventory, *items.findByIndex(Steak), 2);
    REQUIRE_MESSAGE(received.has_value(), errorOf(received));
    CHECK(*received == 120);
    CHECK(inventory.money() == 120);
    CHECK(inventory.countOf(Steak) == 1);
    CHECK_FALSE(sell(inventory, *items.findByIndex(Steak), 2).has_value());
    CHECK_FALSE(sell(inventory, *items.findByIndex(Rod), 1).has_value());

    inventory.addMoney(999999);
    REQUIRE(sell(inventory, *items.findByIndex(Steak), 1).has_value());
    CHECK(inventory.money() == 999999);
}

TEST_CASE("buying a key item says so, for the session's autosave") {
    const auto items = fixtureItems();
    Inventory inventory = originalInventory();
    inventory.addMoney(1000);
    const auto bought = buy(inventory, items, *items.findByIndex(Rod), 1, Adult, StockedItems{{Rod}});
    REQUIRE_MESSAGE(bought.has_value(), errorOf(bought));
    CHECK(bought->keyItem);
    CHECK(inventory.countOf(Rod) == 1);
}
