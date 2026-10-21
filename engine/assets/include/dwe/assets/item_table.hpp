#pragma once

#include <dwe/assets/table_composition.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The table of every item, `data/items` (content/schemas/items.schema.json): the executable's ITEM_PARA (item.h:
// Item). Numbers are kept as the extractor wrote them; the name and description are text for the UI and are not read
// here.
struct ItemEntry {
    std::string id;
    // The item's number in the original: its index in ITEM_PARA and in every per-item table.
    std::int32_t index = 0;
    // The price a shop sells it for; it buys it back for half (script_common.c).
    std::int32_t value = 0;
    // The merit points the exchange asks for it.
    std::int32_t meritValue = 0;
    // The sorting category, 0 to 5 (item.c: sortItems).
    std::int32_t sortingValue = 0;
    // Where it can be used: 0 field, 1 battle, 2 both, 255 never (inventory.c: selectOptionMenu).
    std::int32_t itemColor = 0;
    // 1 when it can be thrown away and sold.
    std::int32_t droppable = 0;
};

struct ItemTable {
    std::vector<ItemEntry> items;

    // The item with this id, or nullptr.
    [[nodiscard]] const ItemEntry* find(std::string_view id) const;

    // The item with this original index, or nullptr.
    [[nodiscard]] const ItemEntry* findByIndex(std::int32_t index) const;
};

// How the packs' files of the table compose: items by id (table_composition.hpp).
inline constexpr std::array<EntrySection, 1> ItemEntrySections{{{.section = "items", .entryKey = "id"}}};
inline constexpr TableComposition ItemComposition{.sections = ItemEntrySections};

// Parses the text of `data/items`. Unknown and missing keys are errors.
core::Result<ItemTable> parseItemTable(std::string_view yaml);

// Reads and parses `data/items` from the virtual file system.
core::Result<ItemTable> loadItemTable(const VirtualFileSystem& vfs);

} // namespace dwe::assets
