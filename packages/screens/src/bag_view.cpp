#include <dwe/screens/bag_view.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace dwe::screens {
namespace {

// The item colour of an item the bag never uses (inventory.c: renderInventoryTop draws it in
// another colour).
constexpr std::int32_t NeverUsedColor = 255;

constexpr std::string_view EmptyField = "empty";
constexpr std::string_view NameField = "name";
constexpr std::string_view DescriptionField = "description";
constexpr std::string_view DescriptionSpeciesField = "description_species";
constexpr std::string_view CountField = "count";
constexpr std::string_view DimmedField = "dimmed";
constexpr std::string_view UsableField = "usable";
constexpr std::string_view DiscardableField = "discardable";
constexpr std::string_view ItemColorField = "item_color";
constexpr std::string_view IconField = "icon";
constexpr std::string_view IconRectField = "icon_rect";

// The items' icons (utils2.c: setItemTexture): ETCDAT/ITEM.TIM, a 16 x 16 cell per item in rows of
// 16, by the item's original number, with the item's palette. A cell that starts at texel 0xF0 is
// 15 texels wide or tall.
constexpr std::string_view ItemIconImage = "/texture/etcdat/item";
constexpr std::int32_t ItemIconSide = 16;
constexpr std::int32_t ItemIconsPerRow = 16;
constexpr std::int32_t LastIconTexel = 0xF0;
constexpr std::string_view CountSuffix = ".count";

bool usableIn(const assets::ItemEntry* entry, const game::inventory::UseRules& rules,
              game::inventory::UseContext context) {
  const std::vector<std::int32_t>& colors =
      context == game::inventory::UseContext::Field ? rules.fieldColors : rules.battleColors;
  return entry != nullptr && std::ranges::contains(colors, entry->itemColor);
}

BagSlotView slotViewOf(const std::optional<game::inventory::Stack>& slot,
                       const assets::ItemTable& items, const NameKeys& names,
                       const game::inventory::UseRules& rules,
                       game::inventory::UseContext context) {
  if (!slot) {
    return {};
  }
  const assets::ItemEntry* entry = &items.at(slot->item);
  ItemIconView icon = itemIconOf(*entry).value_or(ItemIconView{});
  return {.empty = false,
          .item = entry->id,
          .nameKey = names.item(slot->item),
          .descriptionKey = names.itemDescription(slot->item),
          .descriptionSpeciesKey = names.itemDescriptionSpecies(slot->item),
          .count = slot->count,
          .dimmed = entry != nullptr && entry->itemColor == NeverUsedColor,
          .usable = usableIn(entry, rules, context),
          .discardable = context == game::inventory::UseContext::Field &&
                         entry->droppable == rules.discardDroppable,
          .itemColor = entry->itemColor,
          .icon = std::move(icon.image),
          .iconRect = std::move(icon.rect)};
}

} // namespace

std::optional<ItemIconView> itemIconOf(const assets::ItemEntry& entry) {
  const std::optional<assets::ItemEntry::Icon> icon = entry.icon();
  if (!icon) {
    return std::nullopt;
  }
  const std::int32_t u = (icon->cell % ItemIconsPerRow) * ItemIconSide;
  const std::int32_t v = (icon->cell / ItemIconsPerRow) * ItemIconSide;
  const auto side = [](std::int32_t start) {
    return start == LastIconTexel ? ItemIconSide - 1 : ItemIconSide;
  };
  return ItemIconView{.image = std::format("{}?palette={}", ItemIconImage, icon->palette),
                      .rect = std::format("{} {} {} {}", u, v, side(u), side(v))};
}

BagView bagViewOf(const game::inventory::Inventory& inventory, const assets::ItemTable& items,
                  const NameKeys& names, game::inventory::UseContext context) {
  BagView view{.slots = {}, .money = inventory.money()};
  for (const std::optional<game::inventory::Stack>& slot : inventory.slots()) {
    view.slots.push_back(slotViewOf(slot, items, names, inventory.rules().use, context));
  }
  return view;
}

ScreenSettings settingsOf(const BagView& view) {
  ScreenSettings settings{
      {.name = std::string{BagSlotList} + std::string{CountSuffix},
       .value = static_cast<double>(view.slots.size())},
      {.name = std::string{BagMoneySetting}, .value = static_cast<double>(view.money)},
  };
  for (std::size_t index = 0; index < view.slots.size(); ++index) {
    const BagSlotView& slot = view.slots[index];
    settings.push_back(
        {.name = listEntryName(BagSlotList, index, EmptyField), .value = slot.empty});
    settings.push_back(
        {.name = listEntryName(BagSlotList, index, NameField), .value = slot.nameKey});
    settings.push_back({.name = listEntryName(BagSlotList, index, DescriptionField),
                        .value = slot.descriptionKey});
    settings.push_back({.name = listEntryName(BagSlotList, index, DescriptionSpeciesField),
                        .value = slot.descriptionSpeciesKey});
    settings.push_back({.name = listEntryName(BagSlotList, index, CountField),
                        .value = static_cast<double>(slot.count)});
    settings.push_back(
        {.name = listEntryName(BagSlotList, index, DimmedField), .value = slot.dimmed});
    settings.push_back(
        {.name = listEntryName(BagSlotList, index, UsableField), .value = slot.usable});
    settings.push_back(
        {.name = listEntryName(BagSlotList, index, DiscardableField), .value = slot.discardable});
    settings.push_back({.name = listEntryName(BagSlotList, index, ItemColorField),
                        .value = static_cast<double>(slot.itemColor)});
    settings.push_back({.name = listEntryName(BagSlotList, index, IconField), .value = slot.icon});
    settings.push_back(
        {.name = listEntryName(BagSlotList, index, IconRectField), .value = slot.iconRect});
  }
  return settings;
}

} // namespace dwe::screens
