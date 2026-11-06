#include <dwe/assets/item_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view ItemsId = "data/items";
constexpr std::string_view Context = "item table";

ItemEntry readItem(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "an item",
                         {"id", EntryIndexKey, "name", "description", "value", "merit_value",
                          "sorting_value", "item_color", "droppable", "unknown_1e"},
                         {"icon_palette"})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.id = reader.text(reader.field(node, "id")),
          .index = integer(EntryIndexKey),
          .value = integer("value"),
          .meritValue = integer("merit_value"),
          .sortingValue = integer("sorting_value"),
          .itemColor = integer("item_color"),
          .droppable = integer("droppable"),
          .iconPalette = reader.optionalInteger<std::uint8_t>(node, "icon_palette")};
}

} // namespace

ItemTable::ItemTable() : generation_{CompositionGeneration::next()} {}

ItemTable::ItemTable(std::vector<ItemEntry> items)
    : items_{std::move(items)}, generation_{CompositionGeneration::next()} {}

const ItemEntry* ItemTable::find(std::string_view id) const {
  const auto found = std::ranges::find(items_, id, &ItemEntry::id);
  return found == items_.end() ? nullptr : &*found;
}

std::optional<ItemHandle> ItemTable::handleOf(std::string_view id) const {
  const ItemEntry* entry = find(id);
  if (entry == nullptr) {
    return std::nullopt;
  }
  return handleOf(*entry);
}

ItemHandle ItemTable::handleOf(const ItemEntry& entry) const {
  DWE_ASSERT(&entry >= items_.data() && &entry < items_.data() + items_.size());
  return ItemHandle{generation_, static_cast<std::uint32_t>(&entry - items_.data())};
}

bool ItemTable::owns(ItemHandle handle) const {
  return handle.generation_ == generation_ && handle.position_ < items_.size();
}

const ItemEntry& ItemTable::at(ItemHandle handle) const {
  DWE_ASSERT(owns(handle));
  return items_[handle.position_];
}

const ItemEntry* ItemTable::findByOriginalIndex(std::int32_t index) const {
  const auto found = std::ranges::find_if(items_, [index](const ItemEntry& entry) {
    return entry.originalIndex && entry.index == index;
  });
  return found == items_.end() ? nullptr : &*found;
}

core::Result<ItemTable> parseItemTable(std::string_view yaml,
                                       const std::set<std::string, std::less<>>& engineNumbered) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
  }
  YamlReader reader;
  const YamlNode& root = *tree;
  if (!reader.expectKeys(root, "the item table", {"items"})) {
    return std::unexpected{reader.error(Context)};
  }
  std::vector<ItemEntry> entries;
  const std::span<const YamlNode> items = reader.sequence(reader.field(root, "items"), "items");
  for (const YamlNode& item : items) {
    entries.push_back(readItem(reader, item));
    entries.back().originalIndex = !engineNumbered.contains(entries.back().id);
  }
  for (std::size_t i = 0; reader.ok() && i < entries.size(); ++i) {
    for (std::size_t j = 0; j < i; ++j) {
      if (entries[i].id == entries[j].id || entries[i].index == entries[j].index) {
        reader.fail(items[i],
                    std::format("item '{}' ({}) is listed twice", entries[i].id, entries[i].index));
        break;
      }
    }
  }
  auto read = reader.finish(std::move(entries), Context);
  if (!read) {
    return std::unexpected{read.error()};
  }
  return ItemTable{std::move(*read)};
}

core::Result<ItemTable> loadItemTable(const VirtualFileSystem& vfs) {
  auto id = AssetId::parse(ItemsId);
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto composed = readComposedTableText(vfs, *id, ItemComposition);
  if (!composed) {
    return std::unexpected{composed.error()};
  }
  return parseItemTable(composed->text, composed->engineNumbered);
}

} // namespace dwe::assets
