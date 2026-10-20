#include <dwe/game/inventory/inventory.hpp>
#include <dwe/ruleset/random.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <vector>

#include "fixture.hpp"

using namespace dwe::game::inventory;
using namespace dwe::game::inventory::testing;

namespace {

std::vector<std::int32_t> itemsInOrder(const Inventory& inventory) {
    std::vector<std::int32_t> items;
    for (const std::optional<Stack>& slot : inventory.slots()) {
        items.push_back(slot ? slot->item : -1);
    }
    return items;
}

} // namespace

TEST_CASE("a new game's bag has 10 slots, no items and no money") {
    const Inventory inventory = originalInventory();
    CHECK(inventory.slotCount() == 10);
    CHECK(inventory.slots().size() == 10);
    CHECK(inventory.state().slots.size() == 30);
    CHECK(inventory.money() == 0);
    for (const std::optional<Stack>& slot : inventory.slots()) {
        CHECK_FALSE(slot.has_value());
    }
}

TEST_CASE("items join the first stack of their kind, or the first empty slot") {
    Inventory inventory = originalInventory();
    CHECK(inventory.add(Tonic, 3).added == 3);
    CHECK(inventory.add(Bun, 1).added == 1);
    CHECK(inventory.add(Tonic, 2).added == 2);
    CHECK(inventory.countOf(Tonic) == 5);
    CHECK(itemsInOrder(inventory)[0] == Tonic);
    CHECK(itemsInOrder(inventory)[1] == Bun);
    CHECK(inventory.countOf(Steak) == 0);
}

TEST_CASE("a stack holds 99: the rest is lost, and a full stack takes nothing even with empty slots") {
    Inventory inventory = originalInventory();
    REQUIRE(inventory.add(Tonic, 95).added == 95);
    const AddResult over = inventory.add(Tonic, 10);
    CHECK(over.taken());
    CHECK(over.added == 4);
    CHECK(over.lost == 6);
    CHECK(inventory.countOf(Tonic) == 99);
    const AddResult full = inventory.add(Tonic, 1);
    CHECK_FALSE(full.taken());
    CHECK(full.refused == 1);
    CHECK_FALSE(inventory.hasRoomFor(Tonic));
    CHECK(inventory.hasRoomFor(Bun));
}

TEST_CASE("a full bag refuses a new kind of item, which stays where it was") {
    Inventory inventory = originalInventory();
    for (std::int32_t item = 0; item < 10; ++item) {
        REQUIRE(inventory.add(100 + item, 1).taken());
    }
    CHECK_FALSE(inventory.hasRoomFor(Bun));
    const AddResult drop = inventory.add(Bun, 1);
    CHECK_FALSE(drop.taken());
    CHECK(drop.refused == 1);
    CHECK(inventory.hasRoomFor(100));
}

TEST_CASE("a stack that runs out leaves a hole, which the next new item fills") {
    Inventory inventory = originalInventory();
    inventory.add(Tonic, 2);
    inventory.add(Bun, 1);
    inventory.add(Steak, 1);
    inventory.remove(Bun, 5);
    CHECK(itemsInOrder(inventory)[1] == -1);
    inventory.remove(Tonic, 1);
    CHECK(inventory.countOf(Tonic) == 1);
    inventory.add(Floppy, 1);
    CHECK(itemsInOrder(inventory)[1] == Floppy);
}

TEST_CASE("the story grows the bag to 20 and 30 slots, and only to those sizes") {
    Inventory inventory = originalInventory();
    CHECK(inventory.setSlotCount(20).has_value());
    CHECK(inventory.slots().size() == 20);
    CHECK(inventory.setSlotCount(30).has_value());
    CHECK_FALSE(inventory.setSlotCount(25).has_value());
    CHECK_FALSE(inventory.setSlotCount(31).has_value());
}

TEST_CASE("moving swaps two slots; discarding throws away a droppable stack, in the field only") {
    const auto items = fixtureItems();
    Inventory inventory = originalInventory();
    inventory.add(Tonic, 4);
    inventory.add(Rod, 1);
    REQUIRE(inventory.move(0, 5).has_value());
    CHECK(itemsInOrder(inventory)[5] == Tonic);
    CHECK_FALSE(inventory.move(0, 10).has_value());

    CHECK_FALSE(inventory.discard(5, items, UseContext::Battle).has_value());
    CHECK_FALSE(inventory.discard(1, items, UseContext::Field).has_value());
    CHECK_FALSE(inventory.discard(0, items, UseContext::Field).has_value());
    REQUIRE(inventory.discard(5, items, UseContext::Field).has_value());
    CHECK(inventory.countOf(Tonic) == 0);
    CHECK(inventory.countOf(Rod) == 1);
}

TEST_CASE("sorting groups the categories in the mode's order, each by item number, with the holes at the end") {
    const auto items = fixtureItems();
    Inventory inventory = originalInventory();
    // Categories: Steak and Bun 1, Tonic 0, PowerDisk 3, Bandage 2, Charm 5.
    for (const std::int32_t item : {Steak, Floppy, Charm, Tonic, PowerDisk, Bun, Bandage}) {
        inventory.add(item, 1);
    }
    inventory.remove(Floppy, 1);
    REQUIRE(inventory.sort(0, items).has_value());
    // Order 0: categories 0, 1, 3, 2, 5, 4.
    CHECK(itemsInOrder(inventory) ==
          std::vector<std::int32_t>{Tonic, Bun, Steak, PowerDisk, Bandage, Charm, -1, -1, -1, -1});
    REQUIRE(inventory.sort(1, items).has_value());
    // Order 1: categories 2, 5, 0, 1, 4, 3.
    CHECK(itemsInOrder(inventory) ==
          std::vector<std::int32_t>{Bandage, Charm, Tonic, Bun, Steak, PowerDisk, -1, -1, -1, -1});
    CHECK_FALSE(inventory.sort(3, items).has_value());
}

TEST_CASE("a defeat loses 30 % of the occupied slots, rounded down, as whole stacks") {
    Inventory inventory = originalInventory();
    for (std::int32_t item = 0; item < 10; ++item) {
        inventory.add(100 + item, 5);
    }
    dwe::ruleset::Random random{7};
    const std::vector<Stack> lost = inventory.loseOnDefeat(random);
    CHECK(lost.size() == 3);
    for (const Stack& stack : lost) {
        CHECK(stack.count == 5);
        CHECK(inventory.countOf(stack.item) == 0);
    }
    Inventory few = originalInventory();
    few.add(Tonic, 1);
    few.add(Bun, 1);
    few.add(Steak, 1);
    CHECK(few.loseOnDefeat(random).empty());
}

TEST_CASE("money stops at 999999 and never goes below 0") {
    Inventory inventory = originalInventory();
    inventory.addMoney(999990);
    inventory.addMoney(50);
    CHECK(inventory.money() == 999999);
    inventory.spendMoney(1000000);
    CHECK(inventory.money() == 0);
}
