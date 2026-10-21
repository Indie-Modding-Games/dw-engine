#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

} // namespace

TEST_CASE("the user's disc, extracted: every map of the table parses") {
    const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
    if (!mountedContent) {
        MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
        return;
    }
    const std::filesystem::path& content = *mountedContent;
    const dwe::assets::DiskFileSystem disk;
    const auto packs = dwe::assets::findPacks(disk, content);
    REQUIRE_MESSAGE(packs.has_value(), errorOf(packs));
    const auto opened = dwe::assets::openPacks(disk, packs->packs, dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(opened.has_value(), errorOf(opened));
    const auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));

    const auto table = dwe::assets::loadMapTable(*vfs);
    REQUIRE_MESSAGE(table.has_value(), errorOf(table));
    CHECK(table->maps.size() > 200);

    int parsed = 0;
    int withFile = 0;
    int warps = 0;
    int triggers = 0;
    for (const dwe::assets::MapEntry& entry : table->maps) {
        if (!entry.hasFile()) {
            continue;
        }
        ++withFile;
        const auto id = dwe::assets::mapDataId(entry.id);
        REQUIRE_MESSAGE(id.has_value(), errorOf(id));
        if (vfs->resolve(*id) == nullptr) {
            continue; // the US table names 3 files the disc does not have
        }
        const auto map = dwe::assets::loadMapDocument(*vfs, entry.id);
        REQUIRE_MESSAGE(map.has_value(), errorOf(map));
        ++parsed;
        CHECK_MESSAGE(map->assets.background("day") != nullptr, entry.id);
        CHECK(map->warps.size() == 10);
        for (const dwe::assets::MapWarp& warp : map->warps) {
            if (warp.targetMapId) {
                ++warps;
                CHECK_MESSAGE(table->find(*warp.targetMapId) != nullptr, entry.id, " -> ", *warp.targetMapId);
            }
        }
        for (const std::uint8_t value : map->collision.values()) {
            if (value != 0 && (value & dwe::assets::MapCollision::BlockedBit) == 0) {
                ++triggers;
            }
        }
    }
    MESSAGE(parsed, " of ", withFile, " maps with a file parsed; ", warps, " warps with a target; ", triggers,
            " trigger cells");
    CHECK(parsed >= 240);
}
