#pragma once

#include <dwe/assets/image.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/rhi/bindless.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "scene_prims.hpp"

namespace dwe::app {

// The mist of the Misty Trees and of two sewer screens (overworld.c: renderMist, called by
// renderMapOverlays). Two layers of the same picture, the 256 x 200 texels of the 4-bit page at
// VRAM x 0x2c0 (the map's second 4-bit image, or its only one in the sewer), stretched over the
// original's 320 x 240 screen and repeated every screen: a back layer that moves with the camera
// and drifts one pixel left every other frame, and a front layer, mirrored, that moves 12/10 as
// fast as the camera and drifts right. Both rows move with the camera's vertical moves only. The
// back layer adds a quarter of its texels (tinted 0x96, so a little brighter) to the picture, the
// front one all of them (tinted 0x50), in front of the map and the characters (ordering-table entry
// 20) and behind the interface.
class MistLayers {
public:
  // The original's screen, centred on (0, 0): every layer covers it with two quads a side.
  static constexpr std::int32_t ScreenWidth = 0x140;
  static constexpr std::int32_t ScreenHeight = 0xF0;
  // The texels each quad reads (setUVDataPolyFT4(prim, 0, 0, 0xff, 0xc8)).
  static constexpr std::int32_t TexelsWide = 0xFF;
  static constexpr std::int32_t TexelsHigh = 0xC8;
  // The back layer's colour (0x96) and the front's (0x50): 0x80 draws the texels as they are.
  static constexpr std::int32_t BackTint = 0x96;
  static constexpr std::int32_t FrontTint = 0x50;

  // One frame of the original drawn on a mist screen: the camera (MAP_TILE_DATA.cameraX and Y, in
  // background pixels) moved since the last frame drawn on any map, and the play time's frame
  // count (PLAYTIME_FRAMES) is even or not.
  void frame(glm::ivec2 camera, bool evenFrame);
  // A frame drawn on another map: only the camera is kept (map.c: renderMap stores
  // CAMERA_X_PREVIOUS and CAMERA_Y_PREVIOUS on every map).
  void keepCamera(glm::ivec2 camera) { previousCamera_ = camera; }

  // The left edges of the back layer's quads (MIST_X_OFFSETS), the right edges of the front's
  // (MAIN_D_80134228: the quads are mirrored) and the top edges of both rows (MIST_Y_OFFSETS), in
  // screen pixels from the centre.
  [[nodiscard]] const std::array<std::int16_t, 2>& backX() const { return backX_; }
  [[nodiscard]] const std::array<std::int16_t, 2>& frontX() const { return frontX_; }
  [[nodiscard]] const std::array<std::int16_t, 2>& rowsY() const { return rowsY_; }
  // The camera of the last frame drawn.
  [[nodiscard]] glm::ivec2 camera() const { return previousCamera_; }

private:
  // As the game boots (overworld.c: the arrays' first values).
  std::array<std::int16_t, 2> backX_{-0xA0, 0xA0};
  std::array<std::int16_t, 2> frontX_{0xA0, -0xA0};
  std::array<std::int16_t, 2> rowsY_{-0x78, 0x78};
  glm::ivec2 previousCamera_{0};
};

// The palettes the layers read this frame (session::MistLook): a palette of the map's first 4-bit
// page each.
struct MistPalettesShown {
  std::int32_t back = 0;
  std::int32_t front = 0;
};

// The mist's picture with one palette of the map's first 4-bit page (CLUT row 0x1e6, palette i at
// column 16 i: map.c: loadMapImage2), as RGBA that keeps the STP bit of each colour (alpha 128,
// else 255; colour 0x0000 transparent). Nothing when the map has no 4-bit page at VRAM x 0x2c0 or
// no such palette.
[[nodiscard]] std::optional<assets::Image> mistImage(const assets::MapImages& images,
                                                     std::int32_t palette);

// The textures of the layers drawn this frame (mistImage of their palettes).
struct MistTextures {
  std::optional<rhi::TextureId> back;
  std::optional<rhi::TextureId> front;
};

// The screen primitives of both layers, the front one first (AddPrim puts it ahead of the back one
// in the same entry). `shift` moves them by the camera's move since the last frame of the original
// (smooth between frames), in screen pixels; they are repeated and cut to cover `areaMin` to
// `areaMax`, the part of the window the map covers in screen pixels from the centre, so a window
// wider than the original's shows mist to its edges and none on the bars.
[[nodiscard]] std::vector<ScenePrim> mistPrims(const MistLayers& layers,
                                               const MistTextures& textures, glm::vec2 shift,
                                               glm::vec2 areaMin, glm::vec2 areaMax);

} // namespace dwe::app
