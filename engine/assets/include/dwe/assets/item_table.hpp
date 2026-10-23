#pragma once

#include <dwe/assets/table_composition.hpp>
#include <dwe/assets/table_handle.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The table of every item, `data/items` (content/schemas/items.schema.json): the executable's
// ITEM_PARA (item.h: Item). Numbers are kept as the extractor wrote them; the name and description
// are text for the UI and are not read here.
struct ItemEntry {
  std::string id;
  // The item's number: in the original, its index in ITEM_PARA and in every per-item table. The
  // engine numbers the items a pack adds after the original's; their number depends on the packs
  // mounted.
  std::int32_t index = 0;
  // Whether `index` is the original's (the base pack gave it): only those are covered by a rule
  // over the original's numbers, such as a range of a ruleset.
  bool originalIndex = true;
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
  // The palette of its icon, the cell of its index in `texture/etcdat/item` (ITEM_CLUT_DATA;
  // utils2.c: setItemTexture), or nothing for an item without an icon: one a pack adds, or a table
  // extracted before the field existed.
  std::optional<std::uint8_t> iconPalette = std::nullopt;
};

class ItemTable;

// An item of one composition of the tables, as the running game holds it (table_handle.hpp).
using ItemHandle = TableHandle<ItemTable>;

class ItemTable {
public:
  // An empty table.
  ItemTable();

  // A table of these items, a composition of its own.
  explicit ItemTable(std::vector<ItemEntry> items);

  [[nodiscard]] std::span<const ItemEntry> entries() const { return items_; }

  // The item with this id, or nullptr.
  [[nodiscard]] const ItemEntry* find(std::string_view id) const;

  // The handle of the item with this id, if the table has it.
  [[nodiscard]] std::optional<ItemHandle> handleOf(std::string_view id) const;

  // The handle of an entry of this table.
  [[nodiscard]] ItemHandle handleOf(const ItemEntry& entry) const;

  // Whether the handle is of this composition of the table.
  [[nodiscard]] bool owns(ItemHandle handle) const;

  // The item of a handle of this table; a handle of another composition stops the program (it is a
  // bug: the session's tables never change).
  [[nodiscard]] const ItemEntry& at(ItemHandle handle) const;

  // The item with this original number (ItemEntry::originalIndex), or nullptr: for data written by
  // number before the ids, such as an old save. Everything else goes by id or handle.
  [[nodiscard]] const ItemEntry* findByOriginalIndex(std::int32_t index) const;

private:
  std::vector<ItemEntry> items_;
  CompositionGeneration generation_;
};

// How the packs' files of the table compose: items by id (table_composition.hpp).
inline constexpr std::array<EntrySection, 1> ItemEntrySections{
    {{.section = "items", .entryKey = "id"}}};
inline constexpr TableComposition ItemComposition{.sections = ItemEntrySections};

// Parses the text of `data/items`. Unknown and missing keys are errors. The items of
// `engineNumbered` have the number the engine gave them (ComposedTableText); the others, the
// original's.
core::Result<ItemTable>
parseItemTable(std::string_view yaml,
               const std::set<std::string, std::less<>>& engineNumbered = {});

// Reads and parses `data/items` from the virtual file system.
core::Result<ItemTable> loadItemTable(const VirtualFileSystem& vfs);

} // namespace dwe::assets
