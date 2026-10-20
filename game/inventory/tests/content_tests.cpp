// Runs only when DWE_CONTENT names a content folder extracted with dwx (scripts/dev.sh mounts it).

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/shop_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/inventory/item_effects.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <doctest/doctest.h>

#include <cstdlib>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "fixture.hpp"

using namespace dwe::game::inventory;
using dwe::game::inventory::testing::errorOf;

TEST_CASE("the user's disc, extracted: every item and shop loads and every item's effect resolves") {
    const char* content = std::getenv("DWE_CONTENT");
    if (content == nullptr || std::string_view{content}.empty()) {
        MESSAGE("DWE_CONTENT is not set: skipping the test on the extracted content");
        return;
    }
    const dwe::assets::DiskFileSystem disk;
    auto found = dwe::assets::findPacks(disk, content);
    REQUIRE_MESSAGE(found.has_value(), errorOf(found));
    found->packs.push_back({.path = DWE_GAME_PACK, .kind = dwe::assets::FoundPack::Kind::Folder});
    const auto opened = dwe::assets::openPacks(disk, found->packs);
    REQUIRE_MESSAGE(opened.has_value(), errorOf(opened));
    const auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    const dwe::ruleset::SectionChecks checks{{std::string{SectionName}, inventorySectionCheck()}};
    const auto ruleset = dwe::ruleset::loadRuleset(*vfs, dwe::ruleset::OriginalRuleset, checks);
    REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
    const auto inventory = Inventory::fromRuleset(*ruleset);
    REQUIRE_MESSAGE(inventory.has_value(), errorOf(inventory));

    const auto items = dwe::assets::loadItemTable(*vfs);
    REQUIRE_MESSAGE(items.has_value(), errorOf(items));
    const auto table = dwe::assets::loadItemEffectTable(*vfs);
    REQUIRE_MESSAGE(table.has_value(), errorOf(table));
    const auto shops = dwe::assets::loadShopTable(*vfs);
    REQUIRE_MESSAGE(shops.has_value(), errorOf(shops));
    CHECK_FALSE(items->items.empty());
    CHECK(table->effects.size() == items->items.size());
    CHECK_FALSE(shops->shopMaps.empty());
    CHECK_FALSE(shops->shopkeepers.empty());

    const ItemEffects effects{*table, inventory->rules().effects, inventory->rules().quirks};
    std::size_t resolved = 0;
    std::vector<std::string> unmapped;
    for (const dwe::assets::ItemEntry& item : items->items) {
        auto effect = effects.resolve(item.index);
        if (effect) {
            ++resolved;
        } else {
            unmapped.push_back(effect.error().message);
        }
    }
    MESSAGE(items->items.size(), " items, ", resolved, " effects resolved, ", unmapped.size(), " unmapped; ",
            shops->shopMaps.size(), " shop maps, ", shops->shopkeepers.size(), " shopkeepers");
    for (const std::string& problem : unmapped) {
        MESSAGE("unmapped: ", problem);
    }
    CHECK(unmapped.empty());
}
