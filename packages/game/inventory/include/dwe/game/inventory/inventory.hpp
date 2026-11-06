#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/inventory/item_effect.hpp>
#include <dwe/game/inventory/rules.hpp>
#include <dwe/game/inventory/stacking.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace dwe::game::inventory {

// What a save keeps of the inventory.
struct InventoryState {
  // Every slot the bag can have (max_slots); those past slotCount are not shown and not used.
  std::vector<std::optional<Stack>> slots;
  std::int32_t slotCount = 0;
  std::int32_t money = 0;

  friend bool operator==(const InventoryState&, const InventoryState&) = default;
};

// The player's bag and money (item.c: Inventory; MONEY). Items are handles of the session's
// data/items (table_handle.hpp); a save names them by id.
class Inventory {
public:
  // A new game's inventory under these rules.
  Inventory(InventoryRules rules, std::unique_ptr<StackingRule> stacking);

  // A new game's inventory under the `inventory` section of a ruleset.
  static core::Result<Inventory> fromRuleset(const ruleset::Ruleset& ruleset);

  // Takes the rules of a reloaded or tuned ruleset; the bag keeps its items.
  void setRules(InventoryRules rules, std::unique_ptr<StackingRule> stacking);

  [[nodiscard]] const InventoryRules& rules() const { return rules_; }

  // The usable slots, in menu order.
  [[nodiscard]] std::span<const std::optional<Stack>> slots() const;
  [[nodiscard]] std::int32_t slotCount() const { return state_.slotCount; }
  [[nodiscard]] std::int32_t money() const { return state_.money; }
  [[nodiscard]] const InventoryState& state() const { return state_; }

  // Replaces the whole state, as a save holds it; refused if it does not fit the rules.
  core::Result<void> restore(InventoryState state);

  // Gives items to the bag (item.c: giveItem), as a pickup, a chest, a reward or a purchase does.
  AddResult add(assets::ItemHandle item, std::int32_t count);

  // Whether one more of the item would go in: a pickup or a chest leaves the item where it is
  // otherwise.
  [[nodiscard]] bool hasRoomFor(assets::ItemHandle item) const;

  // How many of the item the bag holds: its first stack (item.c: getItemCount).
  [[nodiscard]] std::int32_t countOf(assets::ItemHandle item) const;

  // Takes `count` of the item from every stack of it; a stack that runs out leaves its slot empty
  // (item.c: removeItem).
  void remove(assets::ItemHandle item, std::int32_t count);

  // Swaps two slots (inventory.c: the menu's move, moveItem).
  core::Result<void> move(std::size_t from, std::size_t to);

  // Throws away a slot's whole stack: only in the field and only droppable items (inventory.c:
  // selectOptionMenu, selectDropItem). As the original's removeItem, every other stack of the item
  // loses as many as the slot held.
  core::Result<void> discard(std::size_t slot, const assets::ItemTable& items, UseContext context);

  // Sorts the bag with one of the rules' orders (inventory.c: sortItems).
  core::Result<void> sort(std::size_t mode, const assets::ItemTable& items);

  // Sets the bag's size to one of the rules' slot counts (script_interp.c: set_inventory_size).
  core::Result<void> setSlotCount(std::int32_t count);

  // A defeat's loss (script_common.c: handleItemLoss): defeat.lost_percent of the occupied slots,
  // rounded down and counted once before any loss, are picks of a random occupied slot; each pick
  // takes every stack of that slot's item. Returns the picked stacks, which the recycle shop
  // counts; the item's other stacks are lost with them. Stops early if the bag empties.
  std::vector<Stack> loseOnDefeat(ruleset::Random& random);

  // Adds money up to the rules' limit.
  void addMoney(std::int32_t amount);

  // Takes money, never below 0.
  void spendMoney(std::int32_t amount);

private:
  [[nodiscard]] std::span<std::optional<Stack>> usableSlots();
  [[nodiscard]] std::vector<std::size_t> occupiedSlots() const;
  [[nodiscard]] std::int32_t defeatLossCount() const;
  [[nodiscard]] std::optional<Stack> pickOccupiedStack(ruleset::Random& random) const;
  void removeEveryStackOf(assets::ItemHandle item);

  InventoryRules rules_;
  std::unique_ptr<StackingRule> stacking_;
  InventoryState state_;
};

} // namespace dwe::game::inventory
