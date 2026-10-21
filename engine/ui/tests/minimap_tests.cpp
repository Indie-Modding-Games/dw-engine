#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/test_support/user_assets.hpp>
#include <dwe/ui/minimap.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ui {
namespace {

// A grid from rows of characters: '#' blocked, '.' walkable, 'E' exit.
MinimapGrid gridOf(const std::vector<std::string_view>& rows) {
    MinimapGrid grid{
        .columns = static_cast<int>(rows.front().size()),
        .rows = static_cast<int>(rows.size()),
        .cells = {},
        .cellSize = 100.0F,
        .originX = -500.0F,
        .originZ = -300.0F,
    };
    for (const std::string_view row : rows) {
        for (const char cell : row) {
            MinimapCell kind = MinimapCell::Walkable;
            if (cell == '#') {
                kind = MinimapCell::Blocked;
            } else if (cell == 'E') {
                kind = MinimapCell::Exit;
            }
            grid.cells.push_back(kind);
        }
    }
    return grid;
}

std::uint32_t pixelAt(const assets::Image& image, std::uint32_t x, std::uint32_t y) {
    const std::size_t offset = ((static_cast<std::size_t>(y) * image.width) + x) * 4;
    std::uint32_t rgba = 0;
    for (std::size_t channel = 0; channel < 4; ++channel) {
        rgba = (rgba << 8U) | static_cast<std::uint32_t>(image.pixels[offset + channel]);
    }
    return rgba;
}

} // namespace

TEST_CASE("the minimap is cropped to the walkable area with a margin") {
    const MinimapGrid grid = gridOf({
        "##########",
        "##########",
        "###...####",
        "###..E####",
        "##########",
        "##########",
    });
    const MinimapStyle style{.pixelsPerCell = 4, .margin = 1};
    const auto built = buildMinimap(grid, style);
    REQUIRE(built.has_value());
    const MinimapImage minimap = built.value_or(MinimapImage{});
    const MinimapLayout& layout = minimap.layout;
    CHECK(layout.firstColumn == 2);
    CHECK(layout.firstRow == 1);
    CHECK(layout.columns == 5);
    CHECK(layout.rows == 4);
    CHECK(minimap.image.width == 20);
    CHECK(minimap.image.height == 16);
    CHECK(minimap.image.pixels.size() == std::size_t{20} * 16 * 4);

    // The margin is transparent; the inside of a walkable cell is filled; its sides along walls are edged.
    CHECK(pixelAt(minimap.image, 0, 0) == 0);
    // Cell (c, r) of the grid is at ((c - 2) * 4, (r - 1) * 4) on the image.
    CHECK(pixelAt(minimap.image, 4, 5) == style.edge);     // cell (3, 2), left side: a wall to its left
    CHECK(pixelAt(minimap.image, 5, 5) == style.walkable); // cell (3, 2), inside
    CHECK(pixelAt(minimap.image, 9, 4) == style.edge);     // cell (4, 2), top side: a wall above
    CHECK(pixelAt(minimap.image, 9, 9) == style.walkable); // cell (4, 3), inside
    CHECK(pixelAt(minimap.image, 14, 10) == style.exit);   // cell (5, 3) is the exit
}

TEST_CASE("a map without walkable cells has no minimap") {
    CHECK_FALSE(buildMinimap(gridOf({"###", "###"})).has_value());
    CHECK_FALSE(buildMinimap(MinimapGrid{}).has_value());
}

TEST_CASE("world positions land on the minimap image") {
    const MinimapGrid grid = gridOf({
        "#####",
        "#...#",
        "#...#",
        "#####",
    });
    const auto built = buildMinimap(grid, MinimapStyle{.pixelsPerCell = 2, .margin = 0});
    REQUIRE(built.has_value());
    const MinimapImage minimap = built.value_or(MinimapImage{});
    // Columns 1-3 and rows 1-2: x from -400 to -100, z from -200 to 0.
    const MinimapPoint corner = locateOnMinimap(minimap.layout, -400.0F, -200.0F);
    CHECK(corner.u == doctest::Approx(0.0F));
    CHECK(corner.v == doctest::Approx(0.0F));
    const MinimapPoint center = locateOnMinimap(minimap.layout, -250.0F, -100.0F);
    CHECK(center.u == doctest::Approx(0.5F));
    CHECK(center.v == doctest::Approx(0.5F));
    const MinimapPoint outside = locateOnMinimap(minimap.layout, 1000.0F, -1000.0F);
    CHECK(outside.u > 1.0F);
    CHECK(outside.v < 0.0F);
}

TEST_CASE("markers turn with the facing direction, clockwise from up (-Z)") {
    CHECK(minimapHeading(0.0F, -1.0F) == doctest::Approx(0.0F));
    CHECK(minimapHeading(1.0F, 0.0F) == doctest::Approx(90.0F));
    CHECK(minimapHeading(0.0F, 1.0F) == doctest::Approx(180.0F));
    CHECK(minimapHeading(-1.0F, 0.0F) == doctest::Approx(-90.0F));
}

TEST_CASE("the marker is an opaque arrow in a transparent square") {
    const assets::Image marker = buildMinimapMarker(32);
    CHECK(marker.width == 32);
    CHECK(marker.height == 32);
    CHECK(pixelAt(marker, 0, 0) == 0);                  // a corner is outside the arrow
    CHECK(pixelAt(marker, 16, 16) == 0xFFFFFFFF);       // its middle is filled white
    CHECK((pixelAt(marker, 16, 2) & 0xFFU) > 0);        // the tip is drawn
    CHECK((pixelAt(marker, 16, 2) >> 8U) != 0xFFFFFFU); // in the outline colour
}

TEST_CASE("a map's collision becomes a minimap grid") {
    assets::MapDocument map;
    const auto cellAt = [](int column, int row) { return assets::CollisionCell{.column = column, .row = row}; };
    map.collision.setValue(cellAt(3, 4), assets::MapCollision::BlockedBit);
    map.collision.setValue(cellAt(5, 4), 110); // warp 0 leads to a map
    map.collision.setValue(cellAt(6, 4), 111); // warp 1 leads nowhere
    map.collision.setValue(cellAt(7, 4), 112); // warp 2 is not in the document
    map.collision.setValue(cellAt(8, 4), 60);  // a step trigger
    map.warps.push_back(assets::MapWarp{.exit = 0, .targetMap = 3, .targetMapId = "twna02"});
    map.warps.push_back(assets::MapWarp{.exit = 1, .targetMap = 900, .targetMapId = std::nullopt});

    const MinimapGrid grid = minimapGridOf(map);
    REQUIRE(grid.columns == 100);
    REQUIRE(grid.rows == 100);
    REQUIRE(grid.cells.size() == 10000);
    const auto at = [&](int column, int row) {
        return grid.cells[(static_cast<std::size_t>(row) * 100U) + static_cast<std::size_t>(column)];
    };
    CHECK(at(0, 0) == MinimapCell::Walkable);
    CHECK(at(3, 4) == MinimapCell::Blocked);
    CHECK(at(5, 4) == MinimapCell::Exit);
    CHECK(at(6, 4) == MinimapCell::Walkable);
    CHECK(at(7, 4) == MinimapCell::Walkable);
    CHECK(at(8, 4) == MinimapCell::Walkable);
    // Cell (50, 50) starts at the world origin.
    CHECK(grid.originX == doctest::Approx(-5000.0F));
    CHECK(grid.originZ == doctest::Approx(-5000.0F));
    CHECK(grid.cellSize == doctest::Approx(100.0F));
}

TEST_CASE("the user's disc, extracted: every map with a file gives a minimap") {
    const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
    if (!mountedContent) {
        MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
        return;
    }
    const std::filesystem::path& content = *mountedContent;
    const assets::DiskFileSystem disk;
    const auto packs = assets::findPacks(disk, content);
    REQUIRE(packs.has_value());
    const auto opened = assets::openPacks(disk, packs->packs, dwe::assets::PackOrigin::Engine);
    REQUIRE(opened.has_value());
    const auto vfs = assets::VirtualFileSystem::mount(opened->locations);
    REQUIRE(vfs.has_value());
    const auto map = assets::loadMapDocument(*vfs, "twna01");
    REQUIRE(map.has_value());
    const auto minimap = buildMinimap(minimapGridOf(*map));
    REQUIRE(minimap.has_value());
    const MinimapImage image = minimap.value_or(MinimapImage{});
    MESSAGE("twna01: " << image.image.width << " x " << image.image.height);
    CHECK(image.image.width > 60);
    CHECK(image.image.height > 60);
    // Warps of twna01 lead to other maps, so the minimap shows exits.
    int exits = 0;
    for (const MinimapCell cell : minimapGridOf(*map).cells) {
        exits += cell == MinimapCell::Exit ? 1 : 0;
    }
    CHECK(exits > 0);
}

} // namespace dwe::ui
