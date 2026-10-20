#pragma once

#include <dwe/assets/image.hpp>

namespace dwe::assets {
struct MapDocument;
}

#include <cstdint>
#include <optional>
#include <vector>

namespace dwe::ui {

// What a cell of the minimap shows.
enum class MinimapCell : std::uint8_t {
    Blocked,  // walls and everything outside the walkable area: left transparent
    Walkable, // floor, including triggers that are not exits
    Exit,     // a trigger that leads to another map
};

// The walkable area of a map, as plain values: the minimap knows nothing of map files or of the game. Rows go along
// +Z and columns along +X in world space, so the image has -Z at the top.
struct MinimapGrid {
    int columns = 0;
    int rows = 0;
    std::vector<MinimapCell> cells; // row by row, columns * rows
    float cellSize = 100.0F;        // world units per cell
    // World position of the corner of cell (0, 0) with the smallest x and z.
    float originX = 0.0F;
    float originZ = 0.0F;
};

// The minimap grid of a map: cells with the blocked bit are walls, warp triggers (110 + exit) whose warp leads to a
// map are exits, and every other cell is floor (map_collision.c, tamer.c: checkMapInteraction). The grid covers the
// map's 100 x 100 collision cells in world space, cell (column, row) starting at ((column - 50) * 100, (row - 50) *
// 100).
[[nodiscard]] MinimapGrid minimapGridOf(const assets::MapDocument& map);

// The part of the grid the minimap image shows and how it maps to the world.
struct MinimapLayout {
    int firstColumn = 0;
    int firstRow = 0;
    int columns = 0;
    int rows = 0;
    int pixelsPerCell = 1;
    float cellSize = 100.0F;
    float originX = 0.0F;
    float originZ = 0.0F;
};

struct MinimapImage {
    assets::Image image;
    MinimapLayout layout;
};

// The colours of the minimap image, straight (not premultiplied) RGBA.
struct MinimapStyle {
    int pixelsPerCell = 6;
    // Empty cells kept around the walkable area, so the edge is not cut.
    int margin = 1;
    std::uint32_t walkable = 0xD8E2EEC8;
    std::uint32_t edge = 0xFFFFFFFF;
    std::uint32_t exit = 0xFFB547FF;
};

// Draws the walkable area of a grid, cropped to its bounding box plus the margin: walkable cells filled, the cells
// along walls with an edge line, exits in their own colour. Fails (nullopt) when no cell is walkable.
[[nodiscard]] std::optional<MinimapImage> buildMinimap(const MinimapGrid& grid, const MinimapStyle& style = {});

// Where a world position falls on the minimap image, from 0 to 1 on each axis (u to the right, v down); outside
// that range when the position is outside the image.
struct MinimapPoint {
    float u;
    float v;
};

// A marker for the minimap: an arrow pointing up, white with a dark outline so a tint (RCSS image-color) colours
// only its inside. size is its side in pixels.
[[nodiscard]] assets::Image buildMinimapMarker(std::uint32_t size);

[[nodiscard]] MinimapPoint locateOnMinimap(const MinimapLayout& layout, float worldX, float worldZ);

// The angle to rotate an up-pointing marker so it points along a world-space facing direction on the XZ plane, in
// degrees clockwise.
[[nodiscard]] float minimapHeading(float directionX, float directionZ);

} // namespace dwe::ui
