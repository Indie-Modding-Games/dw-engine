#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/inventory/item_effect.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::screens {

// The settings of the item bag page (ui/partner_menu/document), which refreshes on
// BagRevisionSetting.
inline constexpr std::string_view BagRevisionSetting = "bag.revision";
inline constexpr std::string_view BagSlotList = "bag.slot";
inline constexpr std::string_view BagMoneySetting = "bag.money";
// What the last command on the bag did, as a text key (partner_menu.bag_result.*); empty before
// any.
inline constexpr std::string_view BagResultSetting = "bag.result";

// A slot of the bag as the item list shows it (inventory.c: renderInventoryTop,
// renderInventoryBottom).
struct BagSlotView {
  bool empty = true;
  // The item's id in data/items; empty for an empty slot.
  std::string item;
  std::string nameKey;
  std::string descriptionKey;
  std::int32_t count = 0;
  // Drawn in the other colour: an item that is never used from the bag (item_color 255).
  bool dimmed = false;
  // Whether its item_color lets it be used where the bag is open (inventory.c: selectOptionMenu).
  bool usable = false;

  friend bool operator==(const BagSlotView&, const BagSlotView&) = default;
};

// The bag's usable slots, in menu order, and the money.
struct BagView {
  std::vector<BagSlotView> slots;
  std::int32_t money = 0;
};

// `context` is where the bag is open: the field's menu or the battle.
[[nodiscard]] BagView
bagViewOf(const game::inventory::Inventory& inventory, const assets::ItemTable& items,
          const NameKeys& names,
          game::inventory::UseContext context = game::inventory::UseContext::Field);

[[nodiscard]] ScreenSettings settingsOf(const BagView& view);

} // namespace dwe::screens
