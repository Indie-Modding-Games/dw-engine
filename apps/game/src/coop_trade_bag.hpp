#pragma once

#include <dwe/game/coop/trade.hpp>
#include <dwe/game/inventory/inventory.hpp>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace dwe::session {
class InventoryAdapter;
}

namespace dwe::app {

// A player's bag as a trade sees it (game::coop::TradeBag): the game's inventory, whose items go
// by their id, and only those the original lets go of (droppable: what can be thrown away and
// sold) can be given. The swap is the inventory's own (Inventory::swap), all or nothing; the undo
// swaps back, so what the bag took meanwhile (a claimed chest, a pick-up) stays, and only when the
// swap back no longer fits puts back the slots it had before, the money as it is now.
class InventoryTradeBag final : public game::coop::TradeBag {
public:
  explicit InventoryTradeBag(session::InventoryAdapter* inventory = nullptr)
      : inventory_{inventory} {}

  void attach(session::InventoryAdapter* inventory) { inventory_ = inventory; }
  [[nodiscard]] const session::InventoryAdapter* inventory() const { return inventory_; }
  [[nodiscard]] game::coop::TradeCheck
  check(std::span<const game::coop::TradeItem> give,
        std::span<const game::coop::TradeItem> get) const override;
  game::coop::TradeCheck apply(std::span<const game::coop::TradeItem> give,
                               std::span<const game::coop::TradeItem> get) override;
  void undo() override;

private:
  // What the last apply did, for the undo.
  struct Applied {
    game::inventory::InventoryState before;
    std::vector<game::inventory::Stack> given;
    std::vector<game::inventory::Stack> got;
  };

  session::InventoryAdapter* inventory_;
  std::optional<Applied> applied_;
};

// Whether the bag lets go of an item (the original's rule for throwing one away).
[[nodiscard]] bool tradeable(const session::InventoryAdapter& inventory, std::string_view item);

} // namespace dwe::app
