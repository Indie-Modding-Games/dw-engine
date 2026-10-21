#include <dwe/screens/bag_view.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

namespace dwe::screens {
namespace {

// The item colour of an item the bag never uses (inventory.c: renderInventoryTop draws it in another colour).
constexpr std::int32_t NeverUsedColor = 255;

constexpr std::string_view EmptyField = "empty";
constexpr std::string_view NameField = "name";
constexpr std::string_view DescriptionField = "description";
constexpr std::string_view CountField = "count";
constexpr std::string_view DimmedField = "dimmed";
constexpr std::string_view UsableField = "usable";
constexpr std::string_view CountSuffix = ".count";

bool usableIn(const assets::ItemEntry* entry, const game::inventory::UseRules& rules,
              game::inventory::UseContext context) {
    const std::vector<std::int32_t>& colors =
        context == game::inventory::UseContext::Field ? rules.fieldColors : rules.battleColors;
    return entry != nullptr && std::ranges::contains(colors, entry->itemColor);
}

BagSlotView slotViewOf(const std::optional<game::inventory::Stack>& slot, const assets::ItemTable& items,
                       const NameKeys& names, const game::inventory::UseRules& rules,
                       game::inventory::UseContext context) {
    if (!slot) {
        return {};
    }
    const assets::ItemEntry* entry = items.findByIndex(slot->item);
    return {.empty = false,
            .item = slot->item,
            .nameKey = names.item(slot->item),
            .descriptionKey = names.itemDescription(slot->item),
            .count = slot->count,
            .dimmed = entry != nullptr && entry->itemColor == NeverUsedColor,
            .usable = usableIn(entry, rules, context)};
}

} // namespace

BagView bagViewOf(const game::inventory::Inventory& inventory, const assets::ItemTable& items, const NameKeys& names,
                  game::inventory::UseContext context) {
    BagView view{.slots = {}, .money = inventory.money()};
    for (const std::optional<game::inventory::Stack>& slot : inventory.slots()) {
        view.slots.push_back(slotViewOf(slot, items, names, inventory.rules().use, context));
    }
    return view;
}

ScreenSettings settingsOf(const BagView& view) {
    ScreenSettings settings{
        {.name = std::string{BagSlotList} + std::string{CountSuffix}, .value = static_cast<double>(view.slots.size())},
        {.name = std::string{BagMoneySetting}, .value = static_cast<double>(view.money)},
    };
    for (std::size_t index = 0; index < view.slots.size(); ++index) {
        const BagSlotView& slot = view.slots[index];
        settings.push_back({.name = listEntryName(BagSlotList, index, EmptyField), .value = slot.empty});
        settings.push_back({.name = listEntryName(BagSlotList, index, NameField), .value = slot.nameKey});
        settings.push_back({.name = listEntryName(BagSlotList, index, DescriptionField), .value = slot.descriptionKey});
        settings.push_back(
            {.name = listEntryName(BagSlotList, index, CountField), .value = static_cast<double>(slot.count)});
        settings.push_back({.name = listEntryName(BagSlotList, index, DimmedField), .value = slot.dimmed});
        settings.push_back({.name = listEntryName(BagSlotList, index, UsableField), .value = slot.usable});
    }
    return settings;
}

} // namespace dwe::screens
