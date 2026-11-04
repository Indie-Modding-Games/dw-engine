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

// The walkable area of a map, as plain values: the minimap knows nothing of map files or of the
// game. Rows go along +Z and columns along +X in world space, so the image has -Z at the top.
struct MinimapGrid {
  int columns = 0;
  int rows = 0;
  std::vector<MinimapCell> cells; // row by row, columns * rows
  float cellSize = 100.0F;        // world units per cell
  // World position of the corner of cell (0, 0) with the smallest x and z.
  float originX = 0.0F;
  float originZ = 0.0F;
};

// The minimap grid of a map: cells with the blocked bit are walls, warp triggers (110 + exit) whose
// warp leads to a map are exits, and every other cell is floor (map_collision.c, tamer.c:
// checkMapInteraction). The grid covers the map's 100 x 100 collision cells in world space, cell
// (column, row) starting at ((column - 50) * 100, (row - 50) * 100).
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
  // A glassy light blue over the round minimap's dark disc, with light edges.
  std::uint32_t walkable = 0x86BCE2B0;
  std::uint32_t edge = 0xE8F6FFFF;
  std::uint32_t exit = 0xFFB547FF;
};

// Draws the walkable area of a grid, cropped to its bounding box plus the margin: walkable cells
// filled, the cells along walls with an edge line, exits in their own colour. Fails (nullopt) when
// no cell is walkable.
[[nodiscard]] std::optional<MinimapImage> buildMinimap(const MinimapGrid& grid,
                                                       const MinimapStyle& style = {});

// Where a world position falls on the minimap image, from 0 to 1 on each axis (u to the right, v
// down); outside that range when the position is outside the image.
struct MinimapPoint {
  float u;
  float v;
};

// A marker for the minimap: an arrow pointing up, white with a dark outline so a tint (RCSS
// image-color) colours only its inside. size is its side in pixels.
[[nodiscard]] assets::Image buildMinimapMarker(std::uint32_t size);

[[nodiscard]] MinimapPoint locateOnMinimap(const MinimapLayout& layout, float worldX, float worldZ);

// The round HUD minimap (an engine improvement; the original has none) shows a disc of the map
// centred on the player, north-up: world -Z at the top, as the image is. The original's cameras are
// fixed per map and cut between angles within one, so a map turned with the camera would jump at
// every cut and one turned with the player's facing would disagree with the fixed screen; north-up
// keeps each map's shape the same wherever the camera looks.
//
// The world distance from the disc's centre to its rim: half the map image's longer side, so a
// player in the middle of the map sees all of it, kept between MinViewRadius (small rooms are not
// blown up) and MaxViewRadius (wide maps keep their detail).
inline constexpr float MinimapMinViewRadius = 800.0F;
inline constexpr float MinimapMaxViewRadius = 1800.0F;
[[nodiscard]] float minimapViewRadius(const MinimapLayout& layout);

// The disc's window on the map image, in image coordinates (0 to 1 on each axis): its centre and
// its radius along u and along v (they differ when the image is not square).
struct MinimapDiscView {
  MinimapPoint centre{.u = 0.5F, .v = 0.5F};
  float radiusU = 0.5F;
  float radiusV = 0.5F;
};

[[nodiscard]] MinimapDiscView minimapDiscView(const MinimapLayout& layout, float centreX,
                                              float centreZ, float viewRadius);

// Where a world position falls on the disc: x to the right and y down, 0 at the centre and 1 at
// the rim. A position farther than `rim` is pinned to it, in its direction, so a partner out of
// view still shows on the edge.
struct MinimapDiscPoint {
  float x = 0.0F;
  float y = 0.0F;
  bool pinned = false;
};

[[nodiscard]] MinimapDiscPoint locateOnDisc(float centreX, float centreZ, float viewRadius,
                                            float worldX, float worldZ, float rim);

// The angle to rotate an up-pointing marker so it points along a world-space facing direction on
// the XZ plane, in degrees clockwise.
[[nodiscard]] float minimapHeading(float directionX, float directionZ);

} // namespace dwe::ui
