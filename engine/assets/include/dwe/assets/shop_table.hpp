#pragma once

#include <dwe/core/error.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The shops, `data/shops` (content/schemas/shops.schema.json): the maps that load the shop code and the Digimon that
// keep shop. There is no stock table: item i is sold while trigger 0x180 + i is set (script_common.c:
// shopFillBuyItemList).
struct ShopTable {
    // Map ids, or raw numbers as text the extractor could not name.
    std::vector<std::string> shopMaps;
    // Digimon ids, or raw numbers as text.
    std::vector<std::string> shopkeepers;
};

// Parses the text of `data/shops`. Unknown and missing keys are errors.
core::Result<ShopTable> parseShopTable(std::string_view yaml);

// Reads and parses `data/shops` from the virtual file system.
core::Result<ShopTable> loadShopTable(const VirtualFileSystem& vfs);

} // namespace dwe::assets
