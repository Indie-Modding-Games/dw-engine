#include <dwe/assets/item_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
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
                           {"id", EntryIndexKey, "name", "description", "value", "merit_value", "sorting_value",
                            "item_color", "droppable", "unknown_1e"})) {
        return {};
    }
    const auto integer = [&](std::string_view key) { return reader.integer<std::int32_t>(reader.field(node, key)); };
    return {.id = reader.text(reader.field(node, "id")),
            .index = integer(EntryIndexKey),
            .value = integer("value"),
            .meritValue = integer("merit_value"),
            .sortingValue = integer("sorting_value"),
            .itemColor = integer("item_color"),
            .droppable = integer("droppable")};
}

} // namespace

const ItemEntry* ItemTable::find(std::string_view id) const {
    const auto found = std::ranges::find(items, id, &ItemEntry::id);
    return found == items.end() ? nullptr : &*found;
}

const ItemEntry* ItemTable::findByIndex(std::int32_t index) const {
    const auto found = std::ranges::find(items, index, &ItemEntry::index);
    return found == items.end() ? nullptr : &*found;
}

core::Result<ItemTable> parseItemTable(std::string_view yaml) {
    auto tree = core::parseYaml(yaml);
    if (!tree) {
        return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
    }
    YamlReader reader;
    const YamlNode& root = *tree;
    if (!reader.expectKeys(root, "the item table", {"items"})) {
        return std::unexpected{reader.error(Context)};
    }
    ItemTable table;
    const std::span<const YamlNode> items = reader.sequence(reader.field(root, "items"), "items");
    for (const YamlNode& item : items) {
        table.items.push_back(readItem(reader, item));
    }
    for (std::size_t i = 0; reader.ok() && i < table.items.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (table.items[i].id == table.items[j].id || table.items[i].index == table.items[j].index) {
                reader.fail(items[i],
                            std::format("item '{}' ({}) is listed twice", table.items[i].id, table.items[i].index));
                break;
            }
        }
    }
    return reader.finish(std::move(table), Context);
}

core::Result<ItemTable> loadItemTable(const VirtualFileSystem& vfs) {
    auto id = AssetId::parse(ItemsId);
    if (!id) {
        return std::unexpected{id.error()};
    }
    auto text = readComposedTable(vfs, *id, ItemComposition);
    if (!text) {
        return std::unexpected{text.error()};
    }
    return parseItemTable(*text);
}

} // namespace dwe::assets
