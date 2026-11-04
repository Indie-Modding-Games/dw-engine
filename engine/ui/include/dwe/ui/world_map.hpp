#pragma once

#include <dwe/assets/image.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dwe::ui {

// The island map's picture as plain values: the painter knows nothing of maps or of the game. Areas
// and roads are placed on a canvas of `width` x `height` units, x to the right and y down.
struct WorldMapArea {
  float x = 0.0F;
  float y = 0.0F;
  // How far the area reaches from its centre; the land around it reaches further, so neighbouring
  // areas join into one island.
  float radius = 40.0F;
  // Its land's colour, RGBA (alpha ignored).
  std::uint32_t colour = 0x8FA860FF;
  // Whether the player knows it: an unknown area is drawn faded.
  bool known = true;
};

// A road drawn between the centres of two areas (indices into WorldMapPicture::areas).
struct WorldMapRoad {
  std::size_t from = 0;
  std::size_t to = 0;
};

struct WorldMapPicture {
  float width = 1000.0F;
  float height = 640.0F;
  std::vector<WorldMapArea> areas;
  std::vector<WorldMapRoad> roads;
};

// The look of the painted map, straight (not premultiplied) RGBA.
struct WorldMapStyle {
  // The image's width in pixels; its height keeps the canvas's proportions.
  std::uint32_t pixelsWide = 1500;
  std::uint32_t deepSea = 0x4F7F93FF;
  std::uint32_t shallowSea = 0x8DBBC2FF;
  std::uint32_t coast = 0x3B2E22FF;
  std::uint32_t border = 0x4A3B2CFF;
  std::uint32_t road = 0x6E4F32FF;
  // What an unknown area's colour fades towards, and how far (0 to 1).
  std::uint32_t unknown = 0xC9BFA6FF;
  float unknownFade = 0.62F;
  // Changes the coastline's and the borders' wobble.
  std::uint32_t seed = 7;
};

// The area a point of the canvas belongs to, or nothing for the sea. Land is what lies within reach
// of an area (its radius times a margin, with a wobbly coast); each point of land belongs to the
// area it is closest to relative to the areas' radii.
[[nodiscard]] std::optional<std::size_t> worldMapAreaAt(const WorldMapPicture& picture, float x,
                                                        float y, std::uint32_t seed = 7);

// Paints the island: the sea with its shallows, each area's land in its colour (faded when
// unknown), dotted borders between areas, an inked coastline and dashed roads. Nothing (an empty
// image) for a picture without areas or with an empty canvas.
[[nodiscard]] assets::Image paintWorldMap(const WorldMapPicture& picture,
                                          const WorldMapStyle& style = {});

// Where a canvas point falls on the painted image, from 0 to 1 on each axis (u to the right, v
// down), clamped to the image.
struct WorldMapPoint {
  float u = 0.0F;
  float v = 0.0F;
};
[[nodiscard]] WorldMapPoint locateOnWorldMap(const WorldMapPicture& picture, float x, float y);

} // namespace dwe::ui
