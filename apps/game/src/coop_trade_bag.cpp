// A player's bag as an item trade between co-op players sees it (#360): an engine addition, the
// original has no online play.
#include "coop_trade_bag.hpp"

#include <dwe/assets/item_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/inventory_adapter.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace dwe::app {

namespace coop = game::coop;

namespace {

// The bag's stacks of a trade's items, by the session's item handles; none when an id is unknown.
std::optional<std::vector<game::inventory::Stack>>
stacksOf(const assets::ItemTable& items, std::span<const coop::TradeItem> trade) {
  std::vector<game::inventory::Stack> stacks;
  for (const coop::TradeItem& item : trade) {
    const auto handle = items.handleOf(item.item);
    if (!handle) {
      return std::nullopt;
    }
    stacks.push_back({.item = *handle, .count = item.count});
  }
  return stacks;
}

coop::TradeCheck checkOf(game::inventory::SwapCheck check) {
  switch (check) {
  case game::inventory::SwapCheck::Ok:
    return coop::TradeCheck::Ok;
  case game::inventory::SwapCheck::Missing:
    return coop::TradeCheck::Missing;
  case game::inventory::SwapCheck::NoRoom:
    return coop::TradeCheck::NoRoom;
  }
  return coop::TradeCheck::Missing;
}

} // namespace

bool tradeable(const session::InventoryAdapter& inventory, std::string_view item) {
  const auto handle = inventory.items().handleOf(item);
  return handle && inventory.items().at(*handle).droppable ==
                       inventory.inventory().rules().use.discardDroppable;
}

coop::TradeCheck InventoryTradeBag::check(std::span<const coop::TradeItem> give,
                                          std::span<const coop::TradeItem> get) const {
  if (inventory_ == nullptr) {
    return coop::TradeCheck::Missing;
  }
  if (!std::ranges::all_of(
          give, [&](const coop::TradeItem& item) { return tradeable(*inventory_, item.item); })) {
    return coop::TradeCheck::Untradeable;
  }
  const auto given = stacksOf(inventory_->items(), give);
  const auto got = stacksOf(inventory_->items(), get);
  if (!given) {
    return coop::TradeCheck::Missing;
  }
  if (!got) {
    return coop::TradeCheck::NoRoom;
  }
  return checkOf(inventory_->inventory().canSwap(*given, *got));
}

coop::TradeCheck InventoryTradeBag::apply(std::span<const coop::TradeItem> give,
                                          std::span<const coop::TradeItem> get) {
  const coop::TradeCheck checked = check(give, get);
  if (checked != coop::TradeCheck::Ok) {
    return checked;
  }
  game::inventory::Inventory& inventory = inventory_->inventory();
  Applied applied{.before = inventory.state(),
                  .given = *stacksOf(inventory_->items(), give),
                  .got = *stacksOf(inventory_->items(), get)};
  const auto check = checkOf(inventory.swap(applied.given, applied.got));
  if (check == coop::TradeCheck::Ok) {
    applied_ = std::move(applied);
  }
  return check;
}

void InventoryTradeBag::undo() {
  if (inventory_ == nullptr || !applied_) {
    return;
  }
  Applied applied = *std::exchange(applied_, std::nullopt);
  game::inventory::Inventory& inventory = inventory_->inventory();
  if (inventory.swap(applied.got, applied.given) == game::inventory::SwapCheck::Ok) {
    return;
  }
  // The bag no longer holds what it got (used, thrown away) or has no room for what it gave: the
  // slots go back as they were. Only the items do: money earned meanwhile (a battle's bits) stays.
  applied.before.money = inventory.money();
  if (auto restored = inventory.restore(std::move(applied.before)); !restored) {
    core::logError("coop: could not undo a trade on the bag: {}", restored.error().message);
  }
}

} // namespace dwe::app
