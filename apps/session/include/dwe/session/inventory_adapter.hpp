#pragma once

#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/inventory/item_effects.hpp>
#include <dwe/game/inventory/rules.hpp>
#include <dwe/game/inventory/shop.hpp>
#include <dwe/game/inventory/use.hpp>
#include <dwe/session/item_use_rules.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::session {

class EvolutionAdapter;

// The save section of the bag and the money.
inline constexpr std::string_view InventorySaveName = "inventory";

struct InventoryAdapterDesc {
  // The mounted packs, for data/items and data/item_effects; they must outlive the adapter.
  const assets::VirtualFileSystem* files = nullptr;
  // Tables already built (tests); otherwise they are read once from the packs.
  std::optional<assets::ItemTable> items;
  std::optional<assets::ItemEffectTable> effects;
  // Where evolution items go; without it they do nothing. It must outlive the adapter.
  EvolutionAdapter* evolution = nullptr;
};

// Glues the inventory to the partner and the session (docs/reference/inventory.md): owns the bag,
// gives the items' effects to the partner, saves the bag and asks for the key item autosave.
class InventoryAdapter final : public SessionSystem {
public:
  explicit InventoryAdapter(InventoryAdapterDesc desc);
  ~InventoryAdapter() override;
  InventoryAdapter(const InventoryAdapter&) = delete;
  InventoryAdapter& operator=(const InventoryAdapter&) = delete;
  InventoryAdapter(InventoryAdapter&&) = delete;
  InventoryAdapter& operator=(InventoryAdapter&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  // The bag of the inventory section, with the ruleset's new-game items (bag.new_game_items).
  core::Result<void> startNewGame(GameSession& session) override;

  // Uses one of an item on the partner in the field: it may refuse it; an item used that is a key
  // item asks for the KeyItem autosave.
  core::Result<game::inventory::UseResult> use(GameSession& session, assets::ItemHandle item);
  // The two halves of `use` for the field's feeding scene: offer checks the item and lets the
  // partner refuse it (Accepted or why not), give consumes and applies it once the partner ate it.
  core::Result<game::inventory::UseResult> offer(GameSession& session, assets::ItemHandle item);
  core::Result<game::inventory::UseResult> give(GameSession& session, assets::ItemHandle item);
  // Uses one of an item in battle on `target` (inventory.c: selectOptionMenu with GAME_STATE 1):
  // only the items whose item_color allows battle; the partner never refuses it and it is consumed
  // when thrown (main.c: startThrowingItem).
  core::Result<game::inventory::UseResult> useInBattle(assets::ItemHandle item,
                                                       game::inventory::ItemEffectTarget& target);
  // Buys from a shop, with the baby discount when the partner's level gets it; a key item asks for
  // the autosave.
  core::Result<game::inventory::Purchase> buy(GameSession& session, assets::ItemHandle item,
                                              std::int32_t amount,
                                              const game::inventory::StockTriggers& triggers);
  // A lost battle takes stacks away at random (script_common.c: handleItemLoss), with the session's
  // random numbers.
  std::vector<game::inventory::Stack> loseOnDefeat(GameSession& session);
  // Hears of the stacks a lost battle takes away (the shop's recycle stock keeps them).
  void setLossListener(std::function<void(const std::vector<game::inventory::Stack>&)> listener) {
    lossListener_ = std::move(listener);
  }

  // The bag, for the menus and the other adapters; only after the rules were read.
  [[nodiscard]] game::inventory::Inventory& inventory();
  [[nodiscard]] const game::inventory::Inventory& inventory() const;
  [[nodiscard]] const assets::ItemTable& items() const;
  [[nodiscard]] const ItemUseRules& itemUseRules() const { return itemUse_; }

private:
  class Serializer;
  class PartnerTarget;

  [[nodiscard]] core::Result<game::inventory::Inventory> freshInventory() const;
  core::Result<void> loadTables();

  InventoryAdapterDesc desc_;
  std::optional<ruleset::Section> section_;
  std::optional<game::inventory::Inventory> inventory_;
  std::optional<game::inventory::ItemEffects> effects_;
  ItemUseRules itemUse_;
  std::unique_ptr<Serializer> serializer_;
  std::function<void(const std::vector<game::inventory::Stack>&)> lossListener_;
};

} // namespace dwe::session
