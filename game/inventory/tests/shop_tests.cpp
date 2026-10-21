#include <dwe/game/inventory/shop.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <initializer_list>
#include <ostream>
#include <set>
#include <string_view>
#include <vector>

#include "fixture.hpp"

using namespace dwe::game::inventory;
using namespace dwe::game::inventory::testing;

namespace {

// The story's triggers: the items in stock.
class StockedItems final : public StockTriggers {
public:
    explicit StockedItems(std::initializer_list<std::string_view> items) {
        for (const std::string_view item : items) {
            items_.insert(numberOf(item));
        }
    }
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
    const auto& items = fixtureItems();
    Inventory inventory = originalInventory();
    const StockedItems stock{Tonic, Floppy, Steak};
    inventory.addMoney(500);
    inventory.add(handleOf(Steak), 99);
    const std::vector<ShopOffer> offers = shopStock(inventory, items, stock);
    REQUIRE(offers.size() == 3);
    CHECK(offers[0].item == handleOf(Tonic));
    CHECK(offers[0].buyable);
    // Floppy costs 2000; Steak's stack is full.
    CHECK_FALSE(offers[1].buyable);
    CHECK_FALSE(offers[2].buyable);
}

TEST_CASE("with the bag full only items with a stack that is not full can be bought") {
    const auto& items = fixtureItems();
    Inventory inventory = originalInventory();
    inventory.addMoney(10000);
    for (std::int32_t item = 0; item < 10; ++item) {
        inventory.add(filler(item), 1);
    }
    inventory.remove(filler(0), 1);
    inventory.add(handleOf(Tonic), 1);
    const StockedItems stock{Tonic, Bun};
    const std::vector<ShopOffer> offers = shopStock(inventory, items, stock);
    CHECK(offers[0].buyable);
    CHECK_FALSE(offers[1].buyable);
}

TEST_CASE("buying pays the price, a baby partner pays 90 %, and the amount is limited by stack, money and 99") {
    const auto& items = fixtureItems();
    Inventory inventory = originalInventory();
    const StockedItems stock{Steak};
    const dwe::assets::ItemEntry& steak = items.at(handleOf(Steak));
    inventory.addMoney(1000);
    CHECK(buyPrice(inventory, steak, Adult) == 120);
    CHECK(buyPrice(inventory, steak, Baby) == 108);
    CHECK(maxPurchase(inventory, items, handleOf(Steak), Adult) == 8);
    // The original limits the amount by the full price even with the discount.
    CHECK(maxPurchase(inventory, items, handleOf(Steak), Baby) == 8);
    CHECK(canAfford(inventory, steak, 9, Baby));
    CHECK_FALSE(canAfford(inventory, steak, 9, Adult));

    const auto bought = buy(inventory, items, handleOf(Steak), 5, Baby, stock);
    REQUIRE_MESSAGE(bought.has_value(), errorOf(bought));
    CHECK(bought->paid == 540);
    CHECK(bought->saved == 60);
    CHECK(bought->added.added == 5);
    CHECK_FALSE(bought->keyItem);
    CHECK(inventory.money() == 460);
    CHECK(inventory.countOf(handleOf(Steak)) == 5);

    CHECK_FALSE(buy(inventory, items, handleOf(Steak), 4, Adult, stock).has_value());
    CHECK_FALSE(buy(inventory, items, handleOf(Tonic), 1, Adult, stock).has_value());
}

TEST_CASE("without the quirk a baby's discount counts for the most it can buy") {
    const auto& items = fixtureItems();
    Inventory inventory = originalInventory();
    InventoryRules rules = inventory.rules();
    rules.quirks.babyDiscountLimitedByFullPrice = false;
    inventory.setRules(rules, stackingRules().create(*originalRuleset().section(SectionName), StackingPath).value());
    inventory.addMoney(1000);
    CHECK(maxPurchase(inventory, items, handleOf(Steak), Baby) == 9);
}

TEST_CASE("the stack's room bounds a purchase: 99 less what the bag holds") {
    const auto& items = fixtureItems();
    Inventory inventory = originalInventory();
    inventory.addMoney(999999);
    inventory.add(handleOf(Bun), 95);
    CHECK(maxPurchase(inventory, items, handleOf(Bun), Adult) == 4);
    CHECK(maxPurchase(inventory, items, handleOf(Tonic), Adult) == 99);
}

TEST_CASE("selling pays half the value, only for items that can be thrown away, up to the money's limit") {
    const auto& items = fixtureItems();
    Inventory inventory = originalInventory();
    inventory.add(handleOf(Steak), 3);
    inventory.add(handleOf(Rod), 1);
    const std::vector<SaleOffer> offers = saleOffers(inventory, items);
    REQUIRE(offers.size() == 2);
    CHECK(offers[0].sellable);
    CHECK_FALSE(offers[1].sellable);

    const auto received = sell(inventory, items, handleOf(Steak), 2);
    REQUIRE_MESSAGE(received.has_value(), errorOf(received));
    CHECK(*received == 120);
    CHECK(inventory.money() == 120);
    CHECK(inventory.countOf(handleOf(Steak)) == 1);
    CHECK_FALSE(sell(inventory, items, handleOf(Steak), 2).has_value());
    CHECK_FALSE(sell(inventory, items, handleOf(Rod), 1).has_value());

    inventory.addMoney(999999);
    REQUIRE(sell(inventory, items, handleOf(Steak), 1).has_value());
    CHECK(inventory.money() == 999999);
}

TEST_CASE("buying a key item says so, for the session's autosave") {
    const auto& items = fixtureItems();
    Inventory inventory = originalInventory();
    inventory.addMoney(1000);
    const auto bought = buy(inventory, items, handleOf(Rod), 1, Adult, StockedItems{Rod});
    REQUIRE_MESSAGE(bought.has_value(), errorOf(bought));
    CHECK(bought->keyItem);
    CHECK(inventory.countOf(handleOf(Rod)) == 1);
}

TEST_CASE("an item a pack adds is never in stock, whatever number the engine gave it") {
    std::vector<dwe::assets::ItemEntry> entries{std::from_range, fixtureItems().entries()};
    entries.push_back({.id = "mymod:lantern", .index = 130, .originalIndex = false, .value = 10});
    const dwe::assets::ItemTable items{std::move(entries)};
    Inventory inventory = originalInventory();
    inventory.addMoney(1000);
    // The trigger of number 130 is set, but the lantern's number is the engine's.
    const StockedItems stock{Tonic};
    class Every final : public StockTriggers {
    public:
        [[nodiscard]] bool isSet(std::int32_t /*trigger*/) const override { return true; }
    };
    const std::vector<ShopOffer> offers = shopStock(inventory, items, Every{});
    CHECK(std::ranges::none_of(offers,
                               [&](const ShopOffer& offer) { return items.at(offer.item).id == "mymod:lantern"; }));
    const dwe::assets::ItemEntry* lantern = items.find("mymod:lantern");
    REQUIRE(lantern != nullptr);
    CHECK_FALSE(buy(inventory, items, items.handleOf(*lantern), 1, Adult, Every{}).has_value());
    CHECK(shopStock(inventory, items, stock).size() == 1);
}
