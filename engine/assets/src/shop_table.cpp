#include <dwe/assets/shop_table.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <format>
#include <utility>

#include "document_text.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view ShopsId = "data/shops";
constexpr std::string_view Context = "shop table";

std::vector<std::string> readNames(YamlReader& reader, const YamlNode& root, std::string_view key) {
    std::vector<std::string> names;
    for (const YamlNode& item : reader.sequence(reader.field(root, key), key)) {
        names.push_back(reader.text(item));
    }
    return names;
}

} // namespace

core::Result<ShopTable> parseShopTable(std::string_view yaml) {
    auto tree = core::parseYaml(yaml);
    if (!tree) {
        return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
    }
    YamlReader reader;
    const YamlNode& root = *tree;
    if (!reader.expectKeys(root, "the shop table", {"shop_maps", "shopkeeper_digimon"})) {
        return std::unexpected{reader.error(Context)};
    }
    ShopTable table{.shopMaps = readNames(reader, root, "shop_maps"),
                    .shopkeepers = readNames(reader, root, "shopkeeper_digimon")};
    return reader.finish(std::move(table), Context);
}

core::Result<ShopTable> loadShopTable(const VirtualFileSystem& vfs) {
    auto text = readDocumentText(vfs, ShopsId);
    if (!text) {
        return std::unexpected{text.error()};
    }
    return parseShopTable(*text);
}

} // namespace dwe::assets
