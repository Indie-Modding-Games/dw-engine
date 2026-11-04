#include <dwe/core/assert.hpp>
#include <dwe/game/inventory/inventory.hpp>

#include <algorithm>
#include <utility>

#include "messages.hpp"

namespace dwe::game::inventory {

namespace {

constexpr std::int32_t FullPercent = 100;

bool contains(const std::vector<std::int32_t>& values, std::int32_t value) {
  return std::ranges::find(values, value) != values.end();
}

} // namespace

Inventory::Inventory(InventoryRules rules, std::unique_ptr<StackingRule> stacking)
    : rules_(std::move(rules)), stacking_(std::move(stacking)) {
  state_ = {.slots =
                std::vector<std::optional<Stack>>(static_cast<std::size_t>(rules_.bag.maxSlots)),
            .slotCount = rules_.bag.newGameSlots,
            .money = rules_.bag.newGameMoney};
}

core::Result<Inventory> Inventory::fromRuleset(const ruleset::Ruleset& ruleset) {
  auto section = ruleset.requireSection(SectionName);
  if (!section) {
    return std::unexpected{section.error()};
  }
  auto rules = parseInventoryRules(**section);
  if (!rules) {
    return std::unexpected{rules.error()};
  }
  auto stacking = stackingRules().create(**section, StackingPath);
  if (!stacking) {
    return std::unexpected{stacking.error()};
  }
  return Inventory{std::move(*rules), std::move(*stacking)};
}

void Inventory::setRules(InventoryRules rules, std::unique_ptr<StackingRule> stacking) {
  rules_ = std::move(rules);
  stacking_ = std::move(stacking);
  state_.slots.resize(static_cast<std::size_t>(rules_.bag.maxSlots));
  state_.money = std::min(state_.money, rules_.maxMoney);
  if (!contains(rules_.bag.slotCounts, state_.slotCount)) {
    state_.slotCount = std::min(state_.slotCount, rules_.bag.maxSlots);
  }
}

std::span<const std::optional<Stack>> Inventory::slots() const {
  return std::span{state_.slots}.first(static_cast<std::size_t>(state_.slotCount));
}

std::span<std::optional<Stack>> Inventory::usableSlots() {
  return std::span{state_.slots}.first(static_cast<std::size_t>(state_.slotCount));
}

core::Result<void> Inventory::restore(InventoryState state) {
  if (std::cmp_not_equal(state.slots.size(), rules_.bag.maxSlots)) {
    return core::fail(
        messages::formatMessage(messages::SaveSlotCount, state.slots.size(), rules_.bag.maxSlots));
  }
  if (!contains(rules_.bag.slotCounts, state.slotCount)) {
    return core::fail(messages::formatMessage(messages::NotASlotCount, state.slotCount));
  }
  if (state.money < 0 || state.money > rules_.maxMoney) {
    return core::fail(messages::formatMessage(messages::SaveMoney, rules_.maxMoney));
  }
  for (const std::optional<Stack>& slot : state.slots) {
    if (slot && (slot->count < 1 || slot->count > rules_.bag.maxStack)) {
      return core::fail(messages::formatMessage(messages::SaveStack, rules_.bag.maxStack));
    }
  }
  state_ = std::move(state);
  return {};
}

AddResult Inventory::add(assets::ItemHandle item, std::int32_t count) {
  return stacking_->add(usableSlots(), item, count, rules_.bag.maxStack,
                        rules_.quirks.stackOverflowIsLost);
}

bool Inventory::hasRoomFor(assets::ItemHandle item) const {
  std::vector<std::optional<Stack>> trial{slots().begin(), slots().end()};
  return stacking_->add(trial, item, 1, rules_.bag.maxStack, rules_.quirks.stackOverflowIsLost)
             .added > 0;
}

std::int32_t Inventory::countOf(assets::ItemHandle item) const {
  for (const std::optional<Stack>& slot : slots()) {
    if (slot && slot->item == item) {
      return slot->count;
    }
  }
  return 0;
}

void Inventory::remove(assets::ItemHandle item, std::int32_t count) {
  for (std::optional<Stack>& slot : usableSlots()) {
    if (!slot || slot->item != item) {
      continue;
    }
    if (count < slot->count) {
      slot->count -= count;
    } else {
      slot.reset();
    }
  }
}

core::Result<void> Inventory::move(std::size_t from, std::size_t to) {
  const std::size_t size = slots().size();
  for (const std::size_t slot : {from, to}) {
    if (slot >= size) {
      return core::fail(messages::formatMessage(messages::SlotOutOfRange, slot, size));
    }
  }
  // inventory.c: moveItem swaps the item, its count and its name between the two slots.
  std::swap(state_.slots[from], state_.slots[to]);
  return {};
}

core::Result<void> Inventory::discard(std::size_t slot, const assets::ItemTable& items,
                                      UseContext context) {
  if (slot >= slots().size()) {
    return core::fail(messages::formatMessage(messages::SlotOutOfRange, slot, slots().size()));
  }
  if (context != UseContext::Field) {
    return core::fail(std::string{messages::DiscardOnlyInField});
  }
  const std::optional<Stack> stack = state_.slots[slot];
  if (!stack) {
    return core::fail(messages::formatMessage(messages::EmptySlot, slot));
  }
  const assets::ItemEntry& entry = items.at(stack->item);
  if (entry.droppable != rules_.use.discardDroppable) {
    return core::fail(messages::formatMessage(messages::NotDiscardable, entry.id));
  }
  // inventory.c: selectDropItem's yes (tickInventoryUI, case 7) calls removeItem with the slot's
  // item and count: the slot empties and every other stack of the item loses as many.
  remove(stack->item, stack->count);
  return {};
}

core::Result<void> Inventory::sort(std::size_t mode, const assets::ItemTable& items) {
  if (mode >= rules_.sortOrders.size()) {
    return core::fail(
        messages::formatMessage(messages::UnknownSortMode, mode, rules_.sortOrders.size()));
  }
  // The items of each category by number; items outside the six categories are left where the swaps
  // put them. The items a pack adds have numbers after the original's, so they go after them.
  std::array<std::vector<const assets::ItemEntry*>, SortCategoryCount> categories;
  for (const std::optional<Stack>& slot : slots()) {
    if (!slot) {
      continue;
    }
    const assets::ItemEntry& entry = items.at(slot->item);
    if (entry.sortingValue >= 0 && std::cmp_less(entry.sortingValue, SortCategoryCount)) {
      categories.at(static_cast<std::size_t>(entry.sortingValue)).push_back(&entry);
    }
  }
  std::vector<assets::ItemHandle> desired;
  for (const std::int32_t category : rules_.sortOrders[mode]) {
    std::vector<const assets::ItemEntry*>& members =
        categories.at(static_cast<std::size_t>(category));
    std::ranges::sort(members, {}, &assets::ItemEntry::index);
    for (const assets::ItemEntry* member : members) {
      desired.push_back(items.handleOf(*member));
    }
  }
  // inventory.c: sortItems swaps each wanted item into place from the first slot that holds it.
  std::span<std::optional<Stack>> usable = usableSlots();
  for (std::size_t i = 0; i < desired.size(); ++i) {
    const std::optional<Stack>& current = usable[i];
    if (current.has_value() && current->item == desired[i]) {
      continue;
    }
    const auto holder = std::ranges::find_if(usable, [&](const std::optional<Stack>& slot) {
      return slot.has_value() && slot->item == desired[i];
    });
    std::swap(usable[i], *holder);
  }
  return {};
}

core::Result<void> Inventory::setSlotCount(std::int32_t count) {
  if (!contains(rules_.bag.slotCounts, count)) {
    return core::fail(messages::formatMessage(messages::NotASlotCount, count));
  }
  state_.slotCount = count;
  return {};
}

std::vector<std::size_t> Inventory::occupiedSlots() const {
  std::vector<std::size_t> occupied;
  for (std::size_t i = 0; i < slots().size(); ++i) {
    if (slots()[i]) {
      occupied.push_back(i);
    }
  }
  return occupied;
}

std::int32_t Inventory::defeatLossCount() const {
  return static_cast<std::int32_t>(occupiedSlots().size()) * rules_.defeatLostPercent / FullPercent;
}

std::optional<Stack> Inventory::pickOccupiedStack(ruleset::Random& random) const {
  const std::vector<std::size_t> occupied = occupiedSlots();
  if (occupied.empty()) {
    return std::nullopt;
  }
  const auto pick =
      static_cast<std::size_t>(random.below(static_cast<std::int32_t>(occupied.size())));
  return state_.slots[occupied[pick]];
}

void Inventory::removeEveryStackOf(assets::ItemHandle item) {
  for (std::optional<Stack>& slot : usableSlots()) {
    if (slot && slot->item == item) {
      slot.reset();
    }
  }
}

// script_common.c: handleItemLoss. Once the bag is empty the original's remaining picks find an
// empty slot and change nothing (item.c: removeItem ignores it), so stopping there loses the same.
std::vector<Stack> Inventory::loseOnDefeat(ruleset::Random& random) {
  const std::int32_t losses = defeatLossCount();
  std::vector<Stack> lost;
  for (std::int32_t loss = 0; loss < losses; ++loss) {
    const std::optional<Stack> picked = pickOccupiedStack(random);
    if (!picked) {
      break;
    }
    lost.push_back(*picked);
    removeEveryStackOf(picked->item);
  }
  return lost;
}

void Inventory::addMoney(std::int32_t amount) {
  state_.money = std::min(state_.money + amount, rules_.maxMoney);
}

void Inventory::spendMoney(std::int32_t amount) {
  state_.money = std::max(state_.money - amount, 0);
}

} // namespace dwe::game::inventory
