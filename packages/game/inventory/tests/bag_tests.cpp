#include <dwe/game/inventory/inventory.hpp>
#include <dwe/ruleset/random.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string_view>
#include <utility>
#include <vector>

#include "fixture.hpp"

using namespace dwe::game::inventory;
using namespace dwe::game::inventory::testing;

namespace {

// The id of each slot's item, or Empty.
constexpr std::string_view Empty;

std::vector<std::string_view> itemsInOrder(const Inventory& inventory) {
  std::vector<std::string_view> items;
  for (const std::optional<Stack>& slot : inventory.slots()) {
    items.push_back(slot ? std::string_view{fixtureItems().at(slot->item).id} : Empty);
  }
  return items;
}

// The whole of a percentage, as the rule divides by it.
constexpr std::int32_t Hundred = 100;

Stack stackOf(dwe::assets::ItemHandle item, std::int32_t count) {
  return Stack{.item = item, .count = count};
}

// A bag of the original rules whose first slots hold these stacks, as a save may hold them.
Inventory bagHolding(const std::vector<std::optional<Stack>>& stacks, std::int32_t slotCount) {
  Inventory inventory = originalInventory();
  InventoryState state = inventory.state();
  state.slotCount = slotCount;
  std::ranges::copy(stacks, state.slots.begin());
  REQUIRE(inventory.restore(state).has_value());
  return inventory;
}

// The defeat loss as script_common.c: handleItemLoss words it: the count of losses from the
// occupied slots before any loss; each loss lists the occupied slots again, rolls one and takes
// every stack of its item (item.c: removeItem); a roll over an empty bag takes nothing. Returns the
// rolled stacks.
std::vector<Stack> referenceDefeatLoss(std::vector<std::optional<Stack>>& slots,
                                       std::int32_t lostPercent, dwe::ruleset::Random& random) {
  const auto occupiedOf = [&] {
    std::vector<std::size_t> occupied;
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
      if (slots[slot].has_value()) {
        occupied.push_back(slot);
      }
    }
    return occupied;
  };
  const auto losses = static_cast<std::int32_t>(occupiedOf().size()) * lostPercent / Hundred;
  std::vector<Stack> lost;
  for (std::int32_t loss = 0; loss < losses; ++loss) {
    const std::vector<std::size_t> occupied = occupiedOf();
    const std::int32_t roll = random.below(static_cast<std::int32_t>(occupied.size()));
    const std::optional<Stack> rolled =
        occupied.empty() ? std::nullopt : slots[occupied[static_cast<std::size_t>(roll)]];
    if (!rolled.has_value()) {
      continue;
    }
    lost.push_back(*rolled);
    for (std::optional<Stack>& slot : slots) {
      if (slot.has_value() && slot->item == rolled->item) {
        slot.reset();
      }
    }
  }
  return lost;
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
  CHECK(inventory.add(handleOf(Tonic), 3).added == 3);
  CHECK(inventory.add(handleOf(Bun), 1).added == 1);
  CHECK(inventory.add(handleOf(Tonic), 2).added == 2);
  CHECK(inventory.countOf(handleOf(Tonic)) == 5);
  CHECK(itemsInOrder(inventory)[0] == Tonic);
  CHECK(itemsInOrder(inventory)[1] == Bun);
  CHECK(inventory.countOf(handleOf(Steak)) == 0);
}

TEST_CASE(
    "a stack holds 99: the rest is lost, and a full stack takes nothing even with empty slots") {
  Inventory inventory = originalInventory();
  REQUIRE(inventory.add(handleOf(Tonic), 95).added == 95);
  const AddResult over = inventory.add(handleOf(Tonic), 10);
  CHECK(over.taken());
  CHECK(over.added == 4);
  CHECK(over.lost == 6);
  CHECK(inventory.countOf(handleOf(Tonic)) == 99);
  const AddResult full = inventory.add(handleOf(Tonic), 1);
  CHECK_FALSE(full.taken());
  CHECK(full.refused == 1);
  CHECK_FALSE(inventory.hasRoomFor(handleOf(Tonic)));
  CHECK(inventory.hasRoomFor(handleOf(Bun)));
}

TEST_CASE("a full bag refuses a new kind of item, which stays where it was") {
  Inventory inventory = originalInventory();
  for (std::int32_t item = 0; item < 10; ++item) {
    REQUIRE(inventory.add(filler(item), 1).taken());
  }
  CHECK_FALSE(inventory.hasRoomFor(handleOf(Bun)));
  const AddResult drop = inventory.add(handleOf(Bun), 1);
  CHECK_FALSE(drop.taken());
  CHECK(drop.refused == 1);
  CHECK(inventory.hasRoomFor(filler(0)));
}

TEST_CASE("a stack that runs out leaves a hole, which the next new item fills") {
  Inventory inventory = originalInventory();
  inventory.add(handleOf(Tonic), 2);
  inventory.add(handleOf(Bun), 1);
  inventory.add(handleOf(Steak), 1);
  inventory.remove(handleOf(Bun), 5);
  CHECK(itemsInOrder(inventory)[1] == Empty);
  inventory.remove(handleOf(Tonic), 1);
  CHECK(inventory.countOf(handleOf(Tonic)) == 1);
  inventory.add(handleOf(Floppy), 1);
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
  const auto& items = fixtureItems();
  Inventory inventory = originalInventory();
  inventory.add(handleOf(Tonic), 4);
  inventory.add(handleOf(Rod), 1);
  REQUIRE(inventory.move(0, 5).has_value());
  CHECK(itemsInOrder(inventory)[5] == Tonic);
  CHECK_FALSE(inventory.move(0, 10).has_value());

  CHECK_FALSE(inventory.discard(5, items, UseContext::Battle).has_value());
  CHECK_FALSE(inventory.discard(1, items, UseContext::Field).has_value());
  CHECK_FALSE(inventory.discard(0, items, UseContext::Field).has_value());
  REQUIRE(inventory.discard(5, items, UseContext::Field).has_value());
  CHECK(inventory.countOf(handleOf(Tonic)) == 0);
  CHECK(inventory.countOf(handleOf(Rod)) == 1);
}

TEST_CASE("discarding a slot takes its count from every other stack of the item too") {
  // Measured in the original (2026-11-04, 15_item_option_box): Small Recovery 2 in slot 2 and 5 in
  // slot 5; Drop on slot 2 and Yes left slot 2 empty and slot 5 with 3 (selectDropItem,
  // removeItem).
  const auto& items = fixtureItems();
  Inventory inventory =
      bagHolding({stackOf(handleOf(Steak), 3), std::nullopt, stackOf(handleOf(Tonic), 2),
                  std::nullopt, std::nullopt, stackOf(handleOf(Tonic), 5)},
                 10);
  REQUIRE(inventory.discard(2, items, UseContext::Field).has_value());
  CHECK_FALSE(inventory.slots()[2].has_value());
  REQUIRE(inventory.slots()[5].has_value());
  CHECK(inventory.slots()[5]->count == 3);
  CHECK(inventory.slots()[0]->count == 3);
  // A stack with no more than the discarded count goes too.
  REQUIRE(inventory.discard(5, items, UseContext::Field).has_value());
  CHECK(inventory.countOf(handleOf(Tonic)) == 0);
}

TEST_CASE("the total of an item counts every stack of it, the first stack's count only the first") {
  const Inventory inventory = bagHolding({stackOf(handleOf(Tonic), 2), stackOf(handleOf(Steak), 3),
                                          std::nullopt, stackOf(handleOf(Tonic), 5)},
                                         10);
  CHECK(inventory.totalOf(handleOf(Tonic)) == 7);
  CHECK(inventory.countOf(handleOf(Tonic)) == 2);
  CHECK(inventory.totalOf(handleOf(Steak)) == 3);
  CHECK(inventory.totalOf(handleOf(Bun)) == 0);
}

TEST_CASE(
    "sorting groups the categories in the mode's order, each by item number, with the holes at the end") {
  const auto& items = fixtureItems();
  Inventory inventory = originalInventory();
  // Categories: Steak and Bun 1, Tonic 0, PowerDisk 3, Bandage 2, Charm 5.
  for (const std::string_view item : {Steak, Floppy, Charm, Tonic, PowerDisk, Bun, Bandage}) {
    inventory.add(handleOf(item), 1);
  }
  inventory.remove(handleOf(Floppy), 1);
  REQUIRE(inventory.sort(0, items).has_value());
  // Order 0: categories 0, 1, 3, 2, 5, 4.
  CHECK(itemsInOrder(inventory) == std::vector<std::string_view>{Tonic, Bun, Steak, PowerDisk,
                                                                 Bandage, Charm, Empty, Empty,
                                                                 Empty, Empty});
  REQUIRE(inventory.sort(1, items).has_value());
  // Order 1: categories 2, 5, 0, 1, 4, 3.
  CHECK(itemsInOrder(inventory) == std::vector<std::string_view>{Bandage, Charm, Tonic, Bun, Steak,
                                                                 PowerDisk, Empty, Empty, Empty,
                                                                 Empty});
  CHECK_FALSE(inventory.sort(3, items).has_value());
}

TEST_CASE("a defeat loses 30 % of the occupied slots, rounded down, as whole stacks") {
  Inventory inventory = originalInventory();
  for (std::int32_t item = 0; item < 10; ++item) {
    inventory.add(filler(item), 5);
  }
  dwe::ruleset::Random random{7};
  const std::vector<Stack> lost = inventory.loseOnDefeat(random);
  CHECK(lost.size() == 3);
  for (const Stack& stack : lost) {
    CHECK(stack.count == 5);
    CHECK(inventory.countOf(stack.item) == 0);
  }
  Inventory few = originalInventory();
  few.add(handleOf(Tonic), 1);
  few.add(handleOf(Bun), 1);
  few.add(handleOf(Steak), 1);
  CHECK(few.loseOnDefeat(random).empty());
}

TEST_CASE(
    "a defeat's pick takes every stack of the picked item and returns only the picked stack") {
  // filler_0 and filler_1 have two stacks each; 10 occupied slots lose 3 picks.
  Inventory inventory = bagHolding(
      {stackOf(filler(0), 5), stackOf(filler(1), 5), stackOf(filler(0), 7), stackOf(filler(2), 5),
       stackOf(filler(3), 5), stackOf(filler(1), 9), stackOf(filler(4), 5), stackOf(filler(5), 5),
       stackOf(filler(6), 5), stackOf(filler(7), 5)},
      10);
  dwe::ruleset::Random random{11};
  const std::vector<Stack> lost = inventory.loseOnDefeat(random);
  // Seed 11 picks slots 1, 9 and 6; slot 1's pick also takes filler_1's other stack, in slot 5.
  CHECK(lost ==
        std::vector<Stack>{stackOf(filler(1), 5), stackOf(filler(7), 5), stackOf(filler(4), 5)});
  CHECK(itemsInOrder(inventory) ==
        std::vector<std::string_view>{"filler_0", Empty, "filler_0", "filler_2", "filler_3", Empty,
                                      Empty, "filler_5", "filler_6", Empty});
  CHECK(inventory.countOf(filler(0)) == 5);
}

TEST_CASE("a defeat that empties the bag stops there with the bag empty") {
  // Ten stacks of one item: 3 picks are due, the first takes them all.
  std::vector<std::optional<Stack>> stacks;
  for (std::int32_t count = 1; count <= 10; ++count) {
    stacks.emplace_back(stackOf(filler(0), count));
  }
  Inventory inventory = bagHolding(stacks, 10);
  dwe::ruleset::Random random{5};
  const std::vector<Stack> lost = inventory.loseOnDefeat(random);
  // Seed 5 picks slot 0.
  CHECK(lost == std::vector<Stack>{stackOf(filler(0), 1)});
  CHECK(itemsInOrder(inventory) == std::vector<std::string_view>(10, Empty));
}

TEST_CASE("a defeat loses what the original's rule loses, on random bags with repeated items") {
  constexpr std::int32_t Bags = 300;
  constexpr std::int32_t EmptyChance = 30;
  constexpr std::int32_t Kinds = 4;
  constexpr std::int32_t MaxCount = 99;
  constexpr std::array<std::int32_t, 3> SlotCounts{10, 20, 30};
  dwe::ruleset::Random bagMaker{2024};
  // Bags that ran out of stacks with picks still due: the guard's case must stay covered.
  std::int32_t emptiedEarly = 0;
  for (std::int32_t bag = 0; bag < Bags; ++bag) {
    const std::int32_t slotCount = SlotCounts.at(
        static_cast<std::size_t>(bagMaker.below(static_cast<std::int32_t>(SlotCounts.size()))));
    std::vector<std::optional<Stack>> stacks(static_cast<std::size_t>(slotCount));
    for (std::optional<Stack>& slot : stacks) {
      if (!bagMaker.chance(EmptyChance)) {
        // Rolled one by one: the order of a call's arguments is unspecified.
        const std::int32_t kind = bagMaker.below(Kinds);
        const std::int32_t count = 1 + bagMaker.below(MaxCount);
        slot = stackOf(filler(kind), count);
      }
    }
    Inventory inventory = bagHolding(stacks, slotCount);
    const auto occupied = static_cast<std::int32_t>(std::ranges::count_if(
        stacks, [](const std::optional<Stack>& slot) { return slot.has_value(); }));
    const std::int32_t picksDue = occupied * inventory.rules().defeatLostPercent / Hundred;
    const auto seed = static_cast<std::uint64_t>(bag);
    dwe::ruleset::Random ours{seed};
    dwe::ruleset::Random reference{seed};
    const std::vector<Stack> lost = inventory.loseOnDefeat(ours);
    CHECK(lost == referenceDefeatLoss(stacks, inventory.rules().defeatLostPercent, reference));
    CHECK(std::ranges::equal(inventory.slots(), stacks));
    if (std::cmp_less(lost.size(), picksDue)) {
      CHECK(std::ranges::none_of(
          inventory.slots(), [](const std::optional<Stack>& slot) { return slot.has_value(); }));
      ++emptiedEarly;
    }
  }
  CHECK(emptiedEarly > 0);
}

TEST_CASE("money stops at 999999 and never goes below 0") {
  Inventory inventory = originalInventory();
  inventory.addMoney(999990);
  inventory.addMoney(50);
  CHECK(inventory.money() == 999999);
  inventory.spendMoney(1000000);
  CHECK(inventory.money() == 0);
}
