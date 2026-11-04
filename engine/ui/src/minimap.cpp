#include <dwe/assets/map_document.hpp>
#include <dwe/ui/minimap.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace dwe::ui {
namespace {

void putPixel(assets::Image& image, std::uint32_t x, std::uint32_t y, std::uint32_t rgba) {
  constexpr std::uint32_t BytesPerPixel = 4;
  const std::size_t offset = ((static_cast<std::size_t>(y) * image.width) + x) * BytesPerPixel;
  constexpr std::uint32_t ByteMask = 0xFF;
  constexpr std::array<int, 4> Shifts{24, 16, 8, 0};
  for (std::size_t channel = 0; channel < BytesPerPixel; ++channel) {
    image.pixels[offset + channel] =
        static_cast<std::byte>((rgba >> Shifts.at(channel)) & ByteMask);
  }
}

// Collision triggers 110 to 119 take the player through warps 0 to 9 (tamer.c:
// checkMapInteraction).
constexpr std::uint8_t FirstWarpTrigger = 110;
constexpr std::int32_t WarpCount = 10;

MinimapCell minimapCellOf(const assets::MapDocument& map, assets::CollisionCell cell) {
  switch (map.collision.kind(cell)) {
  case assets::CollisionKind::Blocked:
    return MinimapCell::Blocked;
  case assets::CollisionKind::Free:
    return MinimapCell::Walkable;
  case assets::CollisionKind::Trigger:
    break;
  }
  const std::int32_t exit = static_cast<std::int32_t>(map.collision.value(cell)) - FirstWarpTrigger;
  if (exit < 0 || exit >= WarpCount) {
    return MinimapCell::Walkable;
  }
  const assets::MapWarp* warp = map.warp(exit);
  return warp != nullptr && warp->targetMapId.has_value() ? MinimapCell::Exit
                                                          : MinimapCell::Walkable;
}

} // namespace

MinimapGrid minimapGridOf(const assets::MapDocument& map) {
  constexpr int Size = assets::MapCollision::Size;
  const glm::vec3 origin = assets::collisionCellMin(assets::CollisionCell{.column = 0, .row = 0});
  MinimapGrid grid{
      .columns = Size,
      .rows = Size,
      .cells = {},
      .cellSize = assets::CollisionCellSize,
      .originX = origin.x,
      .originZ = origin.z,
  };
  grid.cells.reserve(static_cast<std::size_t>(Size) * Size);
  for (int row = 0; row < Size; ++row) {
    for (int column = 0; column < Size; ++column) {
      grid.cells.push_back(minimapCellOf(map, assets::CollisionCell{.column = column, .row = row}));
    }
  }
  return grid;
}

std::optional<MinimapImage> buildMinimap(const MinimapGrid& grid, const MinimapStyle& style) {
  if (grid.columns <= 0 || grid.rows <= 0 ||
      grid.cells.size() !=
          static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows)) {
    return std::nullopt;
  }
  auto cellAt = [&](int column, int row) {
    if (column < 0 || row < 0 || column >= grid.columns || row >= grid.rows) {
      return MinimapCell::Blocked;
    }
    return grid.cells[(static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns)) +
                      static_cast<std::size_t>(column)];
  };

  int minColumn = grid.columns;
  int minRow = grid.rows;
  int maxColumn = -1;
  int maxRow = -1;
  for (int row = 0; row < grid.rows; ++row) {
    for (int column = 0; column < grid.columns; ++column) {
      if (cellAt(column, row) != MinimapCell::Blocked) {
        minColumn = std::min(minColumn, column);
        maxColumn = std::max(maxColumn, column);
        minRow = std::min(minRow, row);
        maxRow = std::max(maxRow, row);
      }
    }
  }
  if (maxColumn < 0) {
    return std::nullopt;
  }

  const int pixels = std::max(style.pixelsPerCell, 1);
  MinimapLayout layout{
      .firstColumn = minColumn - style.margin,
      .firstRow = minRow - style.margin,
      .columns = maxColumn - minColumn + 1 + (2 * style.margin),
      .rows = maxRow - minRow + 1 + (2 * style.margin),
      .pixelsPerCell = pixels,
      .cellSize = grid.cellSize,
      .originX = grid.originX,
      .originZ = grid.originZ,
  };
  assets::Image image{
      .width = static_cast<std::uint32_t>(layout.columns * pixels),
      .height = static_cast<std::uint32_t>(layout.rows * pixels),
      .pixels = {},
  };
  image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4);

  for (int row = 0; row < layout.rows; ++row) {
    for (int column = 0; column < layout.columns; ++column) {
      const int gridColumn = layout.firstColumn + column;
      const int gridRow = layout.firstRow + row;
      const MinimapCell cell = cellAt(gridColumn, gridRow);
      if (cell == MinimapCell::Blocked) {
        continue;
      }
      const std::uint32_t fill = cell == MinimapCell::Exit ? style.exit : style.walkable;
      // The edge is drawn inside the walkable cell, on each side that touches a blocked one.
      const bool wallLeft = cellAt(gridColumn - 1, gridRow) == MinimapCell::Blocked;
      const bool wallRight = cellAt(gridColumn + 1, gridRow) == MinimapCell::Blocked;
      const bool wallUp = cellAt(gridColumn, gridRow - 1) == MinimapCell::Blocked;
      const bool wallDown = cellAt(gridColumn, gridRow + 1) == MinimapCell::Blocked;
      for (int y = 0; y < pixels; ++y) {
        for (int x = 0; x < pixels; ++x) {
          const bool onEdge = (wallLeft && x == 0) || (wallRight && x == pixels - 1) ||
                              (wallUp && y == 0) || (wallDown && y == pixels - 1);
          putPixel(image, static_cast<std::uint32_t>((column * pixels) + x),
                   static_cast<std::uint32_t>((row * pixels) + y),
                   onEdge && cell != MinimapCell::Exit ? style.edge : fill);
        }
      }
    }
  }
  return MinimapImage{.image = std::move(image), .layout = layout};
}

assets::Image buildMinimapMarker(std::uint32_t size) {
  struct Point {
    float x;
    float y;
  };
  // The arrow, with y down, in a unit square.
  constexpr std::array<Point, 4> Arrow{
      Point{.x = 0.50F, .y = 0.04F},
      Point{.x = 0.90F, .y = 0.94F},
      Point{.x = 0.50F, .y = 0.72F},
      Point{.x = 0.10F, .y = 0.94F},
  };
  constexpr float Outline = 0.09F;
  constexpr int Samples = 4;
  constexpr std::uint32_t Fill = 0xFFFFFF;
  constexpr std::uint32_t Edge = 0x101418;

  auto inside = [&](Point p) {
    bool odd = false;
    for (std::size_t i = 0, j = Arrow.size() - 1; i < Arrow.size(); j = i++) {
      const Point a = Arrow.at(i);
      const Point b = Arrow.at(j);
      if ((a.y > p.y) != (b.y > p.y) && p.x < ((b.x - a.x) * (p.y - a.y) / (b.y - a.y)) + a.x) {
        odd = !odd;
      }
    }
    return odd;
  };
  auto distanceToEdge = [&](Point p) {
    float best = 1.0F;
    for (std::size_t i = 0, j = Arrow.size() - 1; i < Arrow.size(); j = i++) {
      const Point a = Arrow.at(i);
      const Point b = Arrow.at(j);
      const float dx = b.x - a.x;
      const float dy = b.y - a.y;
      const float t = std::clamp(
          (((p.x - a.x) * dx) + ((p.y - a.y) * dy)) / ((dx * dx) + (dy * dy)), 0.0F, 1.0F);
      best = std::min(best, std::hypot(p.x - (a.x + (t * dx)), p.y - (a.y + (t * dy))));
    }
    return best;
  };

  assets::Image image{.width = size, .height = size, .pixels = {}};
  image.pixels.resize(static_cast<std::size_t>(size) * size * 4);
  for (std::uint32_t y = 0; y < size; ++y) {
    for (std::uint32_t x = 0; x < size; ++x) {
      int covered = 0;
      int filled = 0;
      for (int sy = 0; sy < Samples; ++sy) {
        for (int sx = 0; sx < Samples; ++sx) {
          const Point p{
              .x = (static_cast<float>(x) + ((static_cast<float>(sx) + 0.5F) / Samples)) /
                   static_cast<float>(size),
              .y = (static_cast<float>(y) + ((static_cast<float>(sy) + 0.5F) / Samples)) /
                   static_cast<float>(size),
          };
          if (inside(p)) {
            ++covered;
            if (distanceToEdge(p) > Outline) {
              ++filled;
            }
          }
        }
      }
      if (covered == 0) {
        continue;
      }
      // The colour is the mix of fill and edge samples; alpha is the coverage.
      auto mix = [&](int shift) {
        const auto fill = static_cast<float>((Fill >> shift) & 0xFFU);
        const auto edge = static_cast<float>((Edge >> shift) & 0xFFU);
        const float share = static_cast<float>(filled) / static_cast<float>(covered);
        return static_cast<std::uint32_t>(std::lround((fill * share) + (edge * (1.0F - share))));
      };
      const auto alpha = static_cast<std::uint32_t>((covered * 255) / (Samples * Samples));
      putPixel(image, x, y, (mix(16) << 24U) | (mix(8) << 16U) | (mix(0) << 8U) | alpha);
    }
  }
  return image;
}

assets::Image buildMinimapDot(std::uint32_t size) {
  // A disc filling the square, with a dark ring round its white inside.
  constexpr float Radius = 0.46F;
  constexpr float Outline = 0.16F;
  constexpr int Samples = 4;
  constexpr std::uint32_t Edge = 0x101418;
  assets::Image image{.width = size, .height = size, .pixels = {}};
  image.pixels.resize(static_cast<std::size_t>(size) * size * 4);
  for (std::uint32_t y = 0; y < size; ++y) {
    for (std::uint32_t x = 0; x < size; ++x) {
      int covered = 0;
      int filled = 0;
      for (int sy = 0; sy < Samples; ++sy) {
        for (int sx = 0; sx < Samples; ++sx) {
          const float px = ((static_cast<float>(x) + ((static_cast<float>(sx) + 0.5F) / Samples)) /
                            static_cast<float>(size)) -
                           0.5F;
          const float py = ((static_cast<float>(y) + ((static_cast<float>(sy) + 0.5F) / Samples)) /
                            static_cast<float>(size)) -
                           0.5F;
          const float distance = std::hypot(px, py);
          if (distance <= Radius) {
            ++covered;
            if (distance <= Radius - Outline) {
              ++filled;
            }
          }
        }
      }
      if (covered == 0) {
        continue;
      }
      // The colour is the mix of white and edge samples; alpha is the coverage.
      const float share = static_cast<float>(filled) / static_cast<float>(covered);
      auto mix = [&](int shift) {
        const auto edge = static_cast<float>((Edge >> shift) & 0xFFU);
        return static_cast<std::uint32_t>(std::lround((255.0F * share) + (edge * (1.0F - share))));
      };
      const auto alpha = static_cast<std::uint32_t>((covered * 255) / (Samples * Samples));
      putPixel(image, x, y, (mix(16) << 24U) | (mix(8) << 16U) | (mix(0) << 8U) | alpha);
    }
  }
  return image;
}

MinimapPoint locateOnMinimap(const MinimapLayout& layout, float worldX, float worldZ) {
  if (layout.columns <= 0 || layout.rows <= 0 || layout.cellSize <= 0.0F) {
    return {.u = 0.0F, .v = 0.0F};
  }
  const float column =
      ((worldX - layout.originX) / layout.cellSize) - static_cast<float>(layout.firstColumn);
  const float row =
      ((worldZ - layout.originZ) / layout.cellSize) - static_cast<float>(layout.firstRow);
  return {.u = column / static_cast<float>(layout.columns),
          .v = row / static_cast<float>(layout.rows)};
}

float minimapViewRadius(const MinimapLayout& layout) {
  const float side = static_cast<float>(std::max(layout.columns, layout.rows)) * layout.cellSize;
  return std::clamp(side / 2.0F, MinimapMinViewRadius, MinimapMaxViewRadius);
}

MinimapDiscView minimapDiscView(const MinimapLayout& layout, float centreX, float centreZ,
                                float viewRadius) {
  if (layout.columns <= 0 || layout.rows <= 0 || layout.cellSize <= 0.0F) {
    return {};
  }
  const float radiusCells = viewRadius / layout.cellSize;
  return {.centre = locateOnMinimap(layout, centreX, centreZ),
          .radiusU = radiusCells / static_cast<float>(layout.columns),
          .radiusV = radiusCells / static_cast<float>(layout.rows)};
}

MinimapDiscPoint locateOnDisc(float centreX, float centreZ, float viewRadius, float worldX,
                              float worldZ, float rim) {
  if (viewRadius <= 0.0F) {
    return {};
  }
  // North-up: world +X is right and +Z is down on the disc.
  const float x = (worldX - centreX) / viewRadius;
  const float y = (worldZ - centreZ) / viewRadius;
  const float distance = std::hypot(x, y);
  if (distance <= rim) {
    return {.x = x, .y = y, .pinned = false};
  }
  return {.x = x * rim / distance, .y = y * rim / distance, .pinned = true};
}

float minimapHeading(float directionX, float directionZ) {
  // The image has +X to the right and +Z down, so up on the image is -Z.
  constexpr float DegreesPerRadian = 180.0F / std::numbers::pi_v<float>;
  return std::atan2(directionX, -directionZ) * DegreesPerRadian;
}

} // namespace dwe::ui
