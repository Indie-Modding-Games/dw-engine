#include <dwe/assets/card_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/game/inventory/merit_shop.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "fixture.hpp"

using namespace dwe::game::inventory;
using namespace dwe::game::inventory::testing;

namespace {

using dwe::assets::ItemEntry;
using dwe::assets::ItemTable;

// Made-up items: three with a merit price, out of number order, one without, and fillers.
ItemTable meritItems() {
  std::vector<ItemEntry> entries{
      {.id = "chip", .index = 23, .value = 9999, .meritValue = 800},
      {.id = "recovery", .index = 3, .value = 2500, .meritValue = 20},
      {.id = "plain", .index = 0, .value = 100, .meritValue = 0},
      {.id = "horn", .index = 84, .value = 5000, .meritValue = 500},
  };
  for (std::int32_t filler = 0; filler < 40; ++filler) {
    entries.push_back({.id = "filler" + std::to_string(filler), .index = 200 + filler});
  }
  return ItemTable{std::move(entries)};
}

dwe::assets::ItemHandle handle(const ItemTable& items, std::string_view id) {
  const auto found = items.handleOf(id);
  REQUIRE(found.has_value());
  return *found;
}

} // namespace

TEST_CASE("the merit list is every item with a merit price, in number order") {
  const ItemTable items = meritItems();
  const Inventory inventory = originalInventory();
  const std::vector<MeritOffer> offers = meritOffers(inventory, items, 500);
  REQUIRE(offers.size() == 3);
  CHECK(offers[0].item == handle(items, "recovery"));
  CHECK(offers[0].price == 20);
  CHECK(offers[1].item == handle(items, "chip"));
  CHECK(offers[2].item == handle(items, "horn"));
  // shopFillMeritItemList: a price above the merit is not buyable; 500 reaches 500.
  CHECK(offers[0].buyable);
  CHECK_FALSE(offers[1].buyable);
  CHECK(offers[2].buyable);
}

TEST_CASE("with a full bag only an item with a stack below the most can be taken") {
  const ItemTable items = meritItems();
  Inventory inventory = originalInventory();
  inventory.add(handle(items, "recovery"), 98);
  for (std::int32_t slot = 1; slot < inventory.slotCount(); ++slot) {
    inventory.add(handle(items, "filler" + std::to_string(slot)), 1);
  }
  std::vector<MeritOffer> offers = meritOffers(inventory, items, 9999);
  REQUIRE(offers.size() == 3);
  CHECK(offers[0].buyable);
  CHECK_FALSE(offers[1].buyable);
  CHECK_FALSE(offers[2].buyable);
  // A full stack (99, the original's 0x63) takes no more.
  inventory.add(handle(items, "recovery"), 1);
  offers = meritOffers(inventory, items, 9999);
  CHECK_FALSE(offers[0].buyable);
}

TEST_CASE("a card is worth its merit value, and the merit stops at the most") {
  dwe::assets::CardTable table{
      .prices = {5000, 50},
      .cards = {dwe::assets::CardEntry{
                    .index = 1, .digimon = {}, .rarity = 0, .meritValue = 100, .iconPalette = {}},
                dwe::assets::CardEntry{
                    .index = 2, .digimon = {}, .rarity = 1, .meritValue = 0, .iconPalette = {}}}};
  const CardCollection cards{
      CardRules{.maxCopies = 9, .firstListed = 1, .lastListed = 2, .pack = {}, .lines = {}},
      std::move(table)};
  CHECK(cardMerit(cards, 1) == 100);
  CHECK_FALSE(cardMerit(cards, 2).has_value());
  CHECK_FALSE(cardMerit(cards, 7).has_value());
  // openMeritShop state 8: MERIT += SHOP_VARIABLE, then 9999 at most.
  CHECK(addMerit(100, 30, 9999) == 130);
  CHECK(addMerit(9950, 100, 9999) == 9999);
}
