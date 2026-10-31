#include <dwe/game/battle/effect_runner.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <utility>
#include <vector>

#include "effect_math.hpp"

// What the technique effects draw as the original's own GPU primitives rather than sprites or
// models: the layers, sprites and veils on the screen (battle_effect.c:
// BTL_renderScrollingBackground, BTL_renderParallaxSprites, BTL_renderScreenSprite,
// BTL_renderScreenFade, BTL_renderScreenOverlay), the lines and wireframes (BTL_renderEFELine,
// BTL_renderWireframeGrid, BTL_renderWireframeBox) and the ribbons of light
// (BTL_renderRibbonStrip), as EffectRunner members.
namespace dwe::game::battle {

namespace {

namespace fx = effect_math;

using assets::EffectWidth;

constexpr std::int32_t ByteMask = 0xFF;
constexpr std::int32_t Int16Bytes = 2;

// BTL_renderScrollingBackground: 11 x 9 tiles of 32 pixels (0x1f texels), the scroll wrapping at
// 0x140 x 0x100, the first tile 0xc0 left and 0x98 up of the screen's centre.
constexpr std::int32_t TileSize = 32;
constexpr std::int32_t TileTexels = 0x1F;
constexpr std::int32_t TileMask = 0x1F;
constexpr std::int32_t TileShift = 5;
constexpr std::int32_t TileColumns = 11;
constexpr std::int32_t TileRows = 9;
constexpr std::int32_t ScrollWidth = 0x140;
constexpr std::int32_t ScrollHeight = 0x100;
constexpr std::int32_t TilesLeft = 0xC0;
constexpr std::int32_t TilesUp = 0x98;
// BTL_renderParticleFlashSprite, renderParticleFlash: drawn from entry 0x21 (the sprite call lets
// 0xa on, the drawing stops below 0x21) to the back; (x, y) within 0x400 x 0x200 of the centre;
// scale 1024 is 1. Past 0x400 x 0x200 pixels a copy takes more columns, always 3 more than a
// multiple of 4.
constexpr std::int32_t FlashNearest = 0x21;
constexpr std::int32_t FlashMaxX = 0x400;
constexpr std::int32_t FlashMaxY = 0x200;
constexpr std::int32_t FlashScaleShift = 10;
constexpr std::int32_t FlashColumnBits = 3;
// BTL_loadClutColors: a colour's four 16-bit fields, each 5 bits up from the last.
constexpr std::int32_t ClutColorFields = 4;
constexpr std::int32_t ClutFieldShift = 5;
// Both screen layers sort at entry 0x1e of the ordering table, in front of the world.
constexpr std::int32_t ScreenLayerOrder = 0x1E;

// BTL_renderParallaxSprites: a sprite's place is depth * (its point + the offset) >> 7, wrapped
// as the original wraps it (x % 400, then + 360 below 0, then 0xc8 left; y % 320, + 280, 0xa0
// up); its side size * depth >> 8; its texels 0x1f wide from v 0 with the second palette row.
constexpr std::int32_t ParallaxShift = 7;
constexpr std::int32_t ParallaxSizeShift = 8;
constexpr std::int32_t ParallaxWrapX = 400;
constexpr std::int32_t ParallaxWrapY = 320;
constexpr std::int32_t ParallaxBelowX = 360;
constexpr std::int32_t ParallaxBelowY = 280;
constexpr std::int32_t ParallaxLeft = 0xC8;
constexpr std::int32_t ParallaxUp = 0xA0;
constexpr std::int32_t ParallaxClutRow = 1;
// EfeParallaxSprite: s16 size, x, y, depth, u.
constexpr std::int32_t ParallaxSpriteBytes = 10;

// battle_effect.c: the ribbons: 10 points (SVECTOR: x, y, z, pad), first x -0x708 and 0x190
// apart, y from -0x96 to 0x96 and a drift (pad) from -0x1e to 0x1e; each frame a point whose turn
// it is ((frame + 8 i) % 10 == 0) takes a new drift from -0xf to 0xf, and y and z drift by it
// within -0x96 and 0x96. The strip runs 0x1f4 above and 0xc8 below the points.
constexpr std::int32_t RibbonPoints = static_cast<std::int32_t>(EffectRibbon::Points);
constexpr std::int32_t RibbonPointBytes = 8;
constexpr std::int32_t RibbonFirstX = -0x708;
constexpr std::int32_t RibbonStepX = 0x190;
constexpr std::int32_t RibbonSway = 0x96;
constexpr std::int32_t RibbonFirstDrift = 0x1E;
constexpr std::int32_t RibbonDrift = 0xF;
constexpr std::int32_t RibbonTurns = 10;
constexpr std::int32_t RibbonTurnStep = 8;
constexpr std::int32_t RibbonAbove = 0x1F4;
constexpr std::int32_t RibbonBelow = 0xC8;
// BTL_RIBBON_COLORS: the colour goes from one to the next over 10 frames, round the three.
constexpr std::array<EffectVector, 3> RibbonColors = {
    EffectVector{.x = 0x7C, .y = 0x7C, .z = 0x7C}, EffectVector{.x = 0x00, .y = 0x7C, .z = 0x7C},
    EffectVector{.x = 0x3C, .y = 0x7C, .z = 0x3C}};
constexpr std::int32_t RibbonColorFrames = 10;
// It fades in over frames 1 to 7 and out over 0x17 to 0x1e (4096 is full).
constexpr std::int32_t RibbonFadeInStart = 1;
constexpr std::int32_t RibbonFadeInEnd = 7;
constexpr std::int32_t RibbonFadeOutStart = 0x17;
constexpr std::int32_t RibbonFadeOutEnd = 0x1E;
constexpr std::int32_t RibbonFadeSwitch = 0xF;
constexpr std::int32_t FixedOne = 4096;
constexpr std::int32_t FixedShift = 12;
// BTL_renderScreenSprite: GsSortSprite at an entry of the ordering table (0 to 0xfff), added
// (attribute 0x50000000) in grey 0x80; the turn is in degrees (GsSPRITE rotate / 4096).
constexpr std::int32_t OrderingTableEntries = 0x1000;
constexpr std::int32_t UnsignedShortMask = 0xFFFF;
constexpr std::int32_t DegreesPerTurn = 360;
constexpr std::int32_t AngleUnits = 4096;
// BTL_renderScreenFade: the whole screen at entry 0xffe (behind everything but the background),
// subtracting c / 128 of a texel at VRAM (832, 384) taken as white (255 for 1 as an untextured
// colour).
constexpr std::int32_t FadeOrder = 0xFFE;
constexpr std::int32_t FullColor = 0xFF;
constexpr std::int32_t ColorUnit = 0x80;
// BTL_renderScreenOverlay: mode bit 2 makes it semi-transparent, bits 0 and 1 are the rate.
constexpr std::int32_t OverlaySemiTransparentShift = 2;
constexpr std::int32_t OverlayRateMask = 3;
// BTL_setupFixedCamera: GsSetProjection(0x200) and a view from (0, 0, -0x7d0) at the origin, no
// turn: GsWSMATRIX is the identity moved 0x7d0 along z.
constexpr std::int32_t FixedProjection = 0x200;
constexpr std::int32_t FixedDistance = 0x7D0;
// BTL_renderEFELine: drawn between entries 0x20 and 0x1000 (both out); flag 0x20 draws it four
// times a pixel round (BTL_LINE_OFFSET_X/Y) at half the colour, added (mode 5).
constexpr std::int32_t LineNearest = 0x20;
constexpr std::int32_t LineGlowFlag = 0x20;
constexpr std::array<EffectScreenPoint, 4> LineGlowOffsets = {
    EffectScreenPoint{.x = 1, .y = 0}, EffectScreenPoint{.x = 0, .y = 1},
    EffectScreenPoint{.x = -1, .y = 0}, EffectScreenPoint{.x = 0, .y = -1}};
constexpr std::int32_t Percent = 100;
constexpr std::int32_t LineGlowPercent = 50;
// BTL_renderParallelLines: a line is drawn when both ends are between 0x200 and 0x10000 of view
// depth (worldPosToScreenPos: otz << 2), at entry 0xffd.
constexpr std::int32_t GridNearest = 0x200;
constexpr std::int32_t GridFarthest = 0x10000;
constexpr std::int32_t GridOrder = 0xFFD;
constexpr std::int32_t OtzMask = ~3;
// BTL_renderWireframeBox: the box's corners (BTL_WIREFRAME_BOX_SIGN_X/Y/Z) and its four LINE_F4
// (BTL_WIREFRAME_BOX_LINES), at entry 0x22.
constexpr std::array<EffectVector, 8> BoxSigns = {
    EffectVector{.x = -1, .y = -1, .z = -1}, EffectVector{.x = 1, .y = -1, .z = -1},
    EffectVector{.x = 1, .y = 1, .z = -1},   EffectVector{.x = -1, .y = 1, .z = -1},
    EffectVector{.x = -1, .y = -1, .z = 1},  EffectVector{.x = 1, .y = -1, .z = 1},
    EffectVector{.x = 1, .y = 1, .z = 1},    EffectVector{.x = -1, .y = 1, .z = 1}};
constexpr std::array<std::array<std::size_t, 4>, 4> BoxLines = {
    std::array<std::size_t, 4>{0, 1, 2, 3}, std::array<std::size_t, 4>{5, 4, 7, 6},
    std::array<std::size_t, 4>{1, 5, 6, 2}, std::array<std::size_t, 4>{4, 0, 3, 7}};
constexpr std::int32_t BoxOrder = 0x22;
// The GTE's perspective (RTPS): IR saturates at 0x7fff, SZ at 0xffff, H / SZ at 0x1ffff (also when
// SZ is at most H / 2), the screen x and y at -0x400 and 0x3ff; the ordering-table entry is SZ / 16
// (BTL_projectPositionToScreen: otz << 2 >> 4).
constexpr std::int64_t GteVectorLimit = 0x7FFF;
constexpr std::int64_t GteDepthLimit = 0xFFFF;
constexpr std::int64_t GteDivideLimit = 0x1FFFF;
constexpr std::int64_t GteDivideOne = 0x20000;
constexpr std::int64_t GteScreenMin = -0x400;
constexpr std::int64_t GteScreenMax = 0x3FF;
constexpr std::int32_t GteFractionShift = 16;
constexpr std::int32_t OrderShift = 4;
// EfeInstance: the frame, the transform's position, its rotation.
constexpr std::int32_t PositionOffset = 4;
constexpr std::int32_t RotationOffset = 0x10;

// battle_effect.c: BTL_interpolateClamped: from start to end as t goes from lo to hi.
std::int32_t interpolateClamped(std::int32_t lo, std::int32_t hi, std::int32_t t,
                                std::int32_t start, std::int32_t end) {
  if (hi < lo) {
    std::swap(lo, hi);
  }
  t = std::clamp(t, lo, hi);
  if (lo >= t) {
    return start;
  }
  return start + ((end - start) * (t - lo) / (hi - lo));
}

// The corners of an axis-aligned quad (setXYWH).
std::array<EffectScreenPoint, 4> rectangle(std::int32_t x, std::int32_t y, std::int32_t width,
                                           std::int32_t height) {
  return {EffectScreenPoint{.x = x, .y = y}, EffectScreenPoint{.x = x + width, .y = y},
          EffectScreenPoint{.x = x, .y = y + height},
          EffectScreenPoint{.x = x + width, .y = y + height}};
}

// The extra tiles a side needs to reach a screen edge `half` pixels from the centre, past the
// original's edge at `originalHalf`.
std::int32_t extraTiles(std::int32_t half, std::int32_t originalHalf) {
  return std::max(0, half - originalHalf + TileSize - 1) / TileSize;
}

// A GsSPRITE's 16-bit fields.
std::int32_t int16(std::int32_t value) {
  return static_cast<std::int16_t>(value);
}

// A modulo that stays in [0, divisor).
std::int32_t wrapped(std::int32_t value, std::int32_t divisor) {
  return ((value % divisor) + divisor) % divisor;
}

} // namespace

bool EffectRunner::primOp(Slot& slot, std::string_view name,
                          const std::vector<std::int32_t>& args) {
  if (name == "render_scrolling_background") {
    if (args.size() >= 5) {
      scrollingBackground(slot, args);
    }
  } else if (name == "render_parallax_sprites") {
    if (args.size() >= 3) {
      parallaxSprites(slot, args);
    }
  } else if (name == "init_ribbon_points" && !args.empty()) {
    // battle_effect.c: BTL_initializeRibbonPoints.
    for (std::int32_t i = 0; i < RibbonPoints; ++i) {
      const std::int32_t at = args[0] + (i * RibbonPointBytes);
      write(slot, at, EffectWidth::Int16, (i * RibbonStepX) + RibbonFirstX);
      write(slot, at + Int16Bytes, EffectWidth::Int16, customRandom(-RibbonSway, RibbonSway));
      write(slot, at + (3 * Int16Bytes), EffectWidth::Int16,
            customRandom(-RibbonFirstDrift, RibbonFirstDrift));
      write(slot, at + (2 * Int16Bytes), EffectWidth::Int16, 0);
    }
  } else if (name == "tick_ribbon_points" && !args.empty()) {
    // battle_effect.c: BTL_tickRibbonPoints, on the running instance's frame.
    const std::int32_t frame = read(slot, instanceAddress(0), EffectWidth::Int32);
    for (std::int32_t i = 0; i < RibbonPoints; ++i) {
      const std::int32_t at = args[0] + (i * RibbonPointBytes);
      if ((frame + (i * RibbonTurnStep)) % RibbonTurns == 0) {
        write(slot, at + (3 * Int16Bytes), EffectWidth::Int16,
              customRandom(-RibbonDrift, RibbonDrift));
      }
      const std::int32_t drift = read(slot, at + (3 * Int16Bytes), EffectWidth::Int16);
      for (const std::int32_t axis : {at + Int16Bytes, at + (2 * Int16Bytes)}) {
        const std::int32_t moved = read(slot, axis, EffectWidth::Int16) + drift;
        write(slot, axis, EffectWidth::Int16, std::clamp(moved, -RibbonSway, RibbonSway));
      }
    }
  } else if (name == "render_ribbon") {
    if (args.size() >= 3) {
      ribbon(slot, args);
    }
  } else if (name == "render_screen_sprite") {
    if (args.size() >= 13) {
      screenSprite(slot, args);
    }
  } else if (name == "render_screen_fade" && !args.empty()) {
    screenVeil(slot, args, true);
  } else if (name == "render_screen_overlay" && args.size() >= 3) {
    screenVeil(slot, args, false);
  } else if (name == "project_to_screen" && args.size() >= 4) {
    // battle_effect.c: BTL_projectPositionToScreen: position, x, y and depth out.
    const auto [pixel, depth] = project(vectorAt(slot, args[0]));
    write(slot, args[1], EffectWidth::Int32, pixel.x);
    write(slot, args[2], EffectWidth::Int32, pixel.y);
    write(slot, args[3], EffectWidth::Int32, depth >> OrderShift);
  } else if (name == "setup_fixed_camera") {
    fixedCamera_ = true;
  } else if (name == "restore_camera") {
    fixedCamera_ = false;
  } else if (name == "get_viewport_distance_2" && !args.empty()) {
    // battle_effect.c: BTL_getViewportDistance2: the projection distance now in use.
    write(slot, args[0], EffectWidth::Int32, fixedCamera_ ? FixedProjection : view_.projection);
  } else if (name == "render_line") {
    if (args.size() >= 7) {
      line(slot, args);
    }
  } else if (name == "render_wireframe_grid") {
    if (args.size() >= 10) {
      wireframeGrid(slot, args);
    }
  } else if (name == "render_wireframe_box") {
    if (args.size() >= 3) {
      wireframeBox(slot, args);
    }
  } else if (name == "render_particle_flash") {
    if (args.size() >= 9) {
      particleFlash(slot, args);
    }
  } else if (name == "queue_clut_load" && args.size() >= 4) {
    // battle_effect.c: BTL_addClutLoadPrim: depth, palette row, colours, index: the 16 colours from
    // the index on into the row, by a DR_LOAD at the depth's entry of the ordering table. The
    // console loads them as it draws that entry, so the primitives behind it take them from the
    // next frame on; here every primitive of the frame does.
    loadPalette(slot, args[1], 0, args[2] + (args[3] * Int16Bytes),
                static_cast<std::int32_t>(EffectPaletteRow::Colors), false);
  } else if (name == "load_clut_colors" && args.size() >= 4) {
    // battle_effect.c: BTL_loadClutColors: palette row, first colour, count, colours (LoadImage, at
    // once); a count past 16 or below 1 loads nothing.
    if (args[2] > 0 && args[2] <= static_cast<std::int32_t>(EffectPaletteRow::Colors)) {
      loadPalette(slot, args[0], args[1], args[3], args[2], true);
    }
  } else if (name == "get_camera_rotation" && !args.empty()) {
    // battle_effect.c: BTL_getCameraRotation: the angles (matrixToEuler2) of the camera's
    // view-to-world rotation.
    setVector(slot, args[0], fx::toShort(fx::eulerYXZ(fx::transposed(view_.camera))));
  } else {
    return false;
  }
  return true;
}

// Writes `count` colours from the slot's data at `colors` into palette row `row` from colour
// `first` on: as 16-bit colours, or (`fields`) as four 16-bit values each, red, green, blue and STP
// (BTL_loadClutColors: r | g << 5 | b << 10 | stp << 15, each added). A colour past the row's 16
// lands in VRAM beside the palette, which no effect texture reads, so it is dropped.
void EffectRunner::loadPalette(Slot& slot, std::int32_t row, std::int32_t first,
                               std::int32_t colors, std::int32_t count, bool fields) {
  EffectPaletteRow& palette = slot.palettes[row];
  for (std::int32_t i = 0; i < count; ++i) {
    std::int32_t color = 0;
    if (fields) {
      const std::int32_t at = colors + (i * ClutColorFields * Int16Bytes);
      for (std::int32_t field = 0; field < ClutColorFields; ++field) {
        color += read(slot, at + (field * Int16Bytes), EffectWidth::Int16)
                 << (field * ClutFieldShift);
      }
    } else {
      color = read(slot, colors + (i * Int16Bytes), EffectWidth::Int16);
    }
    const std::int32_t index = first + i;
    if (index >= 0 && index < static_cast<std::int32_t>(EffectPaletteRow::Colors)) {
      palette.colors.at(static_cast<std::size_t>(index)) =
          static_cast<std::uint16_t>(color & UnsignedShortMask);
    }
  }
}

// battle_effect.c: BTL_renderScrollingBackground: tiles, tile offsets, scroll x and y, brightness.
// Each tile is the texture's 32 x 32 square at the u its tile offset gives, added in the
// brightness's grey; the scroll moves the whole grid, which wraps around its 11 x 9 tiles.
void EffectRunner::scrollingBackground(Slot& slot, const std::vector<std::int32_t>& args) {
  const std::int32_t tiles = args[0];
  const std::int32_t offsets = args[1];
  std::int32_t sx = args[2] % ScrollWidth;
  std::int32_t tx = (sx >> TileShift) % TileColumns;
  if (sx < 0) {
    sx += ScrollWidth;
    tx += TileColumns - 1;
  }
  std::int32_t sy = args[3] % ScrollHeight;
  std::int32_t ty = (sy >> TileShift) % TileRows;
  if (sy < 0) {
    sy += ScrollHeight;
    ty += TileRows - 1;
  }
  const std::int32_t grey = args[4] & ByteMask;
  // The tiles are placed from the projection's centre (the drawing offset), as the original's
  // are, even when that leaves part of the screen bare. A wider screen than the original's shows
  // more of the same tiles (none extra at 320 x 240).
  const std::int32_t extraColumns = extraTiles(view_.screen.x / 2, OriginalScreen.x / 2);
  const std::int32_t extraRows = extraTiles(view_.screen.y / 2, OriginalScreen.y / 2);
  const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
  for (std::int32_t row = -extraRows; row < TileRows + extraRows; ++row) {
    const std::int32_t y = (sy & TileMask) - TilesUp + (row * TileSize) + view_.centre.y;
    const std::int32_t tileRow = wrapped(ty + row, TileRows) * TileColumns;
    for (std::int32_t column = -extraColumns; column < TileColumns + extraColumns; ++column) {
      const std::int32_t x = (sx & TileMask) - TilesLeft + (column * TileSize) + view_.centre.x;
      const std::int32_t tile =
          read(slot, tiles + tileRow + wrapped(tx + column, TileColumns), EffectWidth::Int8);
      EffectScreenQuad quad;
      quad.corners = rectangle(x, y, TileSize, TileSize);
      quad.u = read(slot, offsets + (tile * Int16Bytes), EffectWidth::Int16);
      quad.texelWidth = TileTexels;
      quad.texelHeight = TileTexels;
      quad.color = {.x = grey, .y = grey, .z = grey};
      quad.blend = EffectBlend::Add;
      quad.order = ScreenLayerOrder;
      draws_.push_back({.slot = index, .shape = quad});
    }
  }
}

// battle_effect.c: BTL_renderRibbonStrip: points, scale, width (unused). The strip is placed by the
// instance's rotation (RotMatrixYXZ), the scale and its position; its colour goes round
// BTL_RIBBON_COLORS on the instance's frame and fades in and out.
void EffectRunner::ribbon(Slot& slot, const std::vector<std::int32_t>& args) {
  const std::int32_t frame = read(slot, instanceAddress(0), EffectWidth::Int32);
  const auto from = static_cast<std::size_t>(static_cast<std::uint32_t>(frame / RibbonColorFrames) %
                                             RibbonColors.size());
  const EffectVector& a = RibbonColors.at(from);
  const EffectVector& b = RibbonColors.at((from + 1) % RibbonColors.size());
  const std::int32_t step = frame % RibbonColorFrames;
  const std::int32_t fade =
      frame < RibbonFadeSwitch
          ? interpolateClamped(RibbonFadeInStart, RibbonFadeInEnd, frame, 0, FixedOne)
          : interpolateClamped(RibbonFadeOutStart, RibbonFadeOutEnd, frame, FixedOne, 0);
  const auto channel = [&](std::int32_t start, std::int32_t end) {
    // A CVECTOR byte.
    return ((interpolateClamped(0, RibbonColorFrames, step, start, end) & ByteMask) * fade >>
            FixedShift) &
           ByteMask;
  };
  EffectRibbon ribbon;
  ribbon.color = {.x = channel(a.x, b.x), .y = channel(a.y, b.y), .z = channel(a.z, b.z)};
  const EffectMatrix place =
      fx::scaled(fx::rotYXZ(fx::toShort(vectorAt(slot, instanceAddress(RotationOffset)))),
                 vectorAt(slot, args[1]));
  const EffectVector at = vectorAt(slot, instanceAddress(PositionOffset));
  const auto world = [&](const EffectVector& local) {
    const EffectVector turned = fx::apply(place, local);
    return EffectVector{.x = turned.x + at.x, .y = turned.y + at.y, .z = turned.z + at.z};
  };
  for (std::size_t i = 0; i < EffectRibbon::Points; ++i) {
    const std::int32_t point = args[0] + (static_cast<std::int32_t>(i) * RibbonPointBytes);
    const EffectVector local{.x = read(slot, point, EffectWidth::Int16),
                             .y = read(slot, point + Int16Bytes, EffectWidth::Int16),
                             .z = read(slot, point + (2 * Int16Bytes), EffectWidth::Int16)};
    ribbon.top.at(i) = world({.x = local.x, .y = local.y - RibbonAbove, .z = local.z});
    ribbon.bottom.at(i) = world({.x = local.x, .y = local.y + RibbonBelow, .z = local.z});
  }
  draws_.push_back({.slot = static_cast<std::size_t>(&slot - slots_.data()), .shape = ribbon});
}

// battle_effect.c: BTL_renderScreenSprite: x, y, u, v, width, height, pivot x and y, turn, entry,
// scale x and y (4096 is 1), palette row. The sprite's pivot texel lands on (x, y) from the
// projection's centre, the sprite scaled and turned clockwise round it; a negative palette row
// (its negation is the row) places it from the screen's centre instead, whatever the drawing
// offset.
void EffectRunner::screenSprite(Slot& slot, const std::vector<std::int32_t>& args) {
  const std::int32_t order = args[9];
  if (order < 0 || order >= OrderingTableEntries) {
    return;
  }
  const std::int32_t row = args[12];
  EffectScreenPoint at{.x = int16(args[0]), .y = int16(args[1])};
  if (row >= 0) {
    at.x += view_.centre.x;
    at.y += view_.centre.y;
  }
  const std::int32_t width = args[4] & UnsignedShortMask;
  const std::int32_t height = args[5] & UnsignedShortMask;
  const std::int32_t pivotX = int16(args[6]);
  const std::int32_t pivotY = int16(args[7]);
  const std::int32_t angle = args[8] * AngleUnits / DegreesPerTurn;
  const std::int32_t cosine = fx::rcos(angle);
  const std::int32_t sine = fx::rsin(angle);
  const std::int32_t scaleX = int16(args[10]);
  const std::int32_t scaleY = int16(args[11]);
  EffectScreenQuad quad;
  for (std::size_t i = 0; i < quad.corners.size(); ++i) {
    const std::int32_t x = ((((i & 1U) != 0 ? width : 0) - pivotX) * scaleX) >> FixedShift;
    const std::int32_t y = ((((i & 2U) != 0 ? height : 0) - pivotY) * scaleY) >> FixedShift;
    quad.corners.at(i) = {.x = at.x + (((x * cosine) - (y * sine)) >> FixedShift),
                          .y = at.y + (((x * sine) + (y * cosine)) >> FixedShift)};
  }
  quad.u = args[2];
  quad.v = args[3];
  quad.texelWidth = width;
  quad.texelHeight = height;
  quad.clutRow = std::abs(row);
  quad.blend = EffectBlend::Add;
  quad.order = order;
  draws_.push_back({.slot = static_cast<std::size_t>(&slot - slots_.data()), .shape = quad});
}

// battle_effect.c: BTL_renderParticleFlashSprite (x, y, u, v, width, height, scale, entry, palette
// row) and efe.c: renderParticleFlash: the texels drawn four times round (x, y) from the
// projection's centre, mirrored to the right and below, each copy (scale * (side - 1) >> 10) + 1
// pixels a side, added. Each copy is cut in cells as the original cuts it (two rows; more columns
// and rows only past 0x400 x 0x200 pixels), the last cell ending on (x, y); a cell off the screen
// is not drawn.
void EffectRunner::particleFlash(Slot& slot, const std::vector<std::int32_t>& args) {
  const std::int32_t order = args[7];
  if (order < FlashNearest || order >= OrderingTableEntries) {
    return;
  }
  const std::int32_t x = args[0];
  const std::int32_t y = args[1];
  if (std::abs(x) >= FlashMaxX || std::abs(y) >= FlashMaxY) {
    return;
  }
  const std::int32_t scale = int16(args[6]);
  const std::int32_t w1 = args[4] - 1;
  const std::int32_t h1 = args[5] - 1;
  std::int32_t sx = (scale * w1) >> FlashScaleShift;
  std::int32_t sy = (scale * h1) >> FlashScaleShift;
  if (sx <= 0 || sy <= 0) {
    return;
  }
  ++sx;
  ++sy;
  std::int32_t cols = 0;
  if (sx >= FlashMaxX || sy >= FlashMaxY) {
    cols = (((sx + 1) / FlashMaxX) + 1) | FlashColumnBits;
  }
  const std::int32_t rows = ((sy + 1) / FlashMaxY) + 1;
  const std::int32_t cellW = (sx + 1) / (cols + 1);
  const std::int32_t cellH = (sy + 1) / (rows + 1);
  const std::int32_t uStep = (w1 + 1) / (cols + 1);
  const std::int32_t vStep = (h1 + 1) / (rows + 1);
  // The screen's edges from the projection's centre.
  const std::int32_t left = -(std::max(view_.screen.x, OriginalScreen.x) / 2) - view_.centre.x;
  const std::int32_t right = left + std::max(view_.screen.x, OriginalScreen.x);
  const std::int32_t top = -(std::max(view_.screen.y, OriginalScreen.y) / 2) - view_.centre.y;
  const std::int32_t bottom = top + std::max(view_.screen.y, OriginalScreen.y);
  const auto copy = [&](bool mirrorX, bool mirrorY) {
    const std::int32_t stepX = mirrorX ? -cellW : cellW;
    const std::int32_t stepY = mirrorY ? -cellH : cellH;
    for (std::int32_t j = 0; j <= rows; ++j) {
      const std::int32_t py = (mirrorY ? y + sy : y - sy) + (stepY * j);
      if (std::max(py, py + stepY) < top || std::min(py, py + stepY) > bottom) {
        continue;
      }
      for (std::int32_t i = 0; i <= cols; ++i) {
        const std::int32_t px = (mirrorX ? x + sx : x - sx) + (stepX * i);
        if (std::max(px, px + stepX) < left || std::min(px, px + stepX) > right) {
          continue;
        }
        const std::int32_t farX = i == cols ? x : px + stepX;
        const std::int32_t farY = j == rows ? y : py + stepY;
        EffectScreenQuad quad;
        quad.corners = {EffectScreenPoint{.x = px, .y = py}, EffectScreenPoint{.x = farX, .y = py},
                        EffectScreenPoint{.x = px, .y = farY},
                        EffectScreenPoint{.x = farX, .y = farY}};
        for (EffectScreenPoint& corner : quad.corners) {
          corner.x += view_.centre.x;
          corner.y += view_.centre.y;
        }
        quad.u = args[2] + (uStep * i);
        quad.v = args[3] + (vStep * j);
        quad.texelWidth = uStep - 1;
        quad.texelHeight = vStep - 1;
        quad.clutRow = args[8];
        quad.blend = EffectBlend::Add;
        quad.order = order;
        draws_.push_back({.slot = static_cast<std::size_t>(&slot - slots_.data()), .shape = quad});
      }
    }
  };
  copy(false, false);
  copy(true, false);
  copy(false, true);
  copy(true, true);
}

// battle_effect.c: BTL_renderScreenFade (brightness) and BTL_renderScreenOverlay (entry, colour,
// mode): a veil over the screen. The fade is placed from the screen's corner, whatever the drawing
// offset, and subtracts its grey; the overlay is placed from the projection's centre, as the
// original's is, in its colour, mixed by its mode. Both cover the original's 320 x 240 and, on a
// wider screen, as much more of it.
void EffectRunner::screenVeil(Slot& slot, const std::vector<std::int32_t>& args, bool fade) {
  const EffectScreenPoint extra{.x = std::max(0, view_.screen.x - OriginalScreen.x) / 2,
                                .y = std::max(0, view_.screen.y - OriginalScreen.y) / 2};
  EffectScreenPoint corner{.x = -(OriginalScreen.x / 2) - extra.x,
                           .y = -(OriginalScreen.y / 2) - extra.y};
  EffectScreenQuad quad;
  quad.textured = false;
  if (fade) {
    const std::int32_t grey = std::min(FullColor, (args[0] & ByteMask) * FullColor / ColorUnit);
    quad.color = {.x = grey, .y = grey, .z = grey};
    quad.blend = EffectBlend::Subtract;
    quad.order = FadeOrder;
  } else {
    const EffectVector color = vectorAt(slot, args[1]);
    quad.color = {.x = color.x & ByteMask, .y = color.y & ByteMask, .z = color.z & ByteMask};
    const std::int32_t mode = args[2];
    quad.blend = (mode >> OverlaySemiTransparentShift) != 0
                     ? static_cast<EffectBlend>(mode & OverlayRateMask)
                     : EffectBlend::Opaque;
    quad.order = args[0];
    corner.x += view_.centre.x;
    corner.y += view_.centre.y;
  }
  quad.corners = rectangle(corner.x, corner.y, OriginalScreen.x + (2 * extra.x),
                           OriginalScreen.y + (2 * extra.y));
  draws_.push_back({.slot = static_cast<std::size_t>(&slot - slots_.data()), .shape = quad});
}

std::pair<EffectScreenPoint, std::int32_t> EffectRunner::project(const EffectVector& world) const {
  EffectMatrix fixed;
  fixed.t = {.x = 0, .y = 0, .z = FixedDistance};
  const EffectMatrix& camera = fixedCamera_ ? fixed : view_.camera;
  const EffectVector turned = fx::apply(camera, fx::toShort(world));
  const auto limited = [](std::int64_t value) {
    return std::clamp(value, -GteVectorLimit, GteVectorLimit);
  };
  const std::int64_t x = limited(std::int64_t{turned.x} + camera.t.x);
  const std::int64_t y = limited(std::int64_t{turned.y} + camera.t.y);
  const std::int64_t z =
      std::clamp(std::int64_t{turned.z} + camera.t.z, std::int64_t{0}, GteDepthLimit);
  const std::int64_t h = fixedCamera_ ? FixedProjection : view_.projection;
  const std::int64_t q =
      h < z * 2 ? std::min((((h * GteDivideOne) / z) + 1) / 2, GteDivideLimit) : GteDivideLimit;
  const auto screen = [&](std::int64_t value) {
    return static_cast<std::int32_t>(
        std::clamp((value * q) >> GteFractionShift, GteScreenMin, GteScreenMax));
  };
  return {EffectScreenPoint{.x = screen(x), .y = screen(y)}, static_cast<std::int32_t>(z)};
}

// battle_effect.c: BTL_renderEFELine: x0, y0, x1, y1 (from the projection's centre, as drawLine2P
// takes them), colour, entry, flags.
void EffectRunner::line(Slot& slot, const std::vector<std::int32_t>& args) {
  const std::int32_t order = args[5];
  if (order <= LineNearest || order >= OrderingTableEntries) {
    return;
  }
  const EffectVector color = vectorAt(slot, args[4]);
  const auto at = [&](std::int32_t x, std::int32_t y, EffectScreenPoint offset) {
    return EffectScreenPoint{.x = x + offset.x + view_.centre.x,
                             .y = y + offset.y + view_.centre.y};
  };
  const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
  if ((args[6] & LineGlowFlag) == 0) {
    draws_.push_back(
        {.slot = index,
         .shape = EffectScreenLine{
             .from = at(args[0], args[1], {}),
             .to = at(args[2], args[3], {}),
             .color = {.x = color.x & ByteMask, .y = color.y & ByteMask, .z = color.z & ByteMask},
             .order = order}});
    return;
  }
  const auto half = [](std::int32_t channel) {
    return (channel * LineGlowPercent / Percent) & ByteMask;
  };
  for (const EffectScreenPoint& offset : LineGlowOffsets) {
    draws_.push_back({.slot = index,
                      .shape = EffectScreenLine{
                          .from = at(args[0], args[1], offset),
                          .to = at(args[2], args[3], offset),
                          .color = {.x = half(color.x), .y = half(color.y), .z = half(color.z)},
                          .blend = EffectBlend::Add,
                          .order = order}});
  }
}

// battle_effect.c: BTL_renderWireframeGrid: origin, angles, colour, x1, x2, count across, z1, z2,
// count along, y. The rectangle (x1..x2, y, z1..z2), turned by the angles (RotMatrixZYX) and moved
// to the origin, is crossed by count + 1 lines each way.
void EffectRunner::wireframeGrid(Slot& slot, const std::vector<std::int32_t>& args) {
  const EffectVector origin = fx::toShort(vectorAt(slot, args[0]));
  const EffectMatrix turn = fx::rotZYX(fx::toShort(vectorAt(slot, args[1])));
  // An array of three int32 (BTL_renderParallelLines: setRGB0 of col[0..2]).
  const EffectVector color = vectorAt(slot, args[2]);
  const auto corner = [&](std::int32_t x, std::int32_t z) {
    return fx::toShort(fx::apply(turn, {.x = int16(x), .y = int16(args[9]), .z = int16(z)}));
  };
  const EffectVector p0 = corner(args[3], args[6]);
  const EffectVector p1 = corner(args[4], args[6]);
  const EffectVector p2 = corner(args[3], args[7]);
  const auto moved = [&](const EffectVector& p) {
    return EffectVector{.x = origin.x + p.x, .y = origin.y + p.y, .z = origin.z + p.z};
  };
  const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
  const auto step = [](const EffectVector& from, const EffectVector& to, std::int32_t count) {
    return EffectVector{.x = int16((to.x - from.x) / count),
                        .y = int16((to.y - from.y) / count),
                        .z = int16((to.z - from.z) / count)};
  };
  const std::int32_t across = int16(args[5]);
  const std::int32_t along = int16(args[8]);
  if (across != 0) {
    parallelLines(index, moved(p0), moved(p2), across, step(p0, p1, across), color);
  }
  if (along != 0) {
    parallelLines(index, moved(p0), moved(p1), along, step(p0, p2, along), color);
  }
}

// battle_effect.c: BTL_renderParallelLines: count + 1 lines from a to b, both moving by the step,
// placed from the screen's centre whatever the drawing offset, opaque at entry 0xffd.
void EffectRunner::parallelLines(std::size_t slot, EffectVector a, EffectVector b,
                                 std::int32_t count, const EffectVector& step,
                                 const EffectVector& color) {
  const auto near = [](std::int32_t depth) {
    depth &= OtzMask;
    return depth > GridNearest && depth < GridFarthest;
  };
  for (std::int32_t i = 0; i <= count; ++i) {
    const auto [from, fromDepth] = project(a);
    const auto [to, toDepth] = project(b);
    if (near(fromDepth) && near(toDepth)) {
      draws_.push_back(
          {.slot = slot,
           .shape = EffectScreenLine{
               .from = from,
               .to = to,
               .color = {.x = color.x & ByteMask, .y = color.y & ByteMask, .z = color.z & ByteMask},
               .order = GridOrder}});
    }
    a = fx::toShort({.x = a.x + step.x, .y = a.y + step.y, .z = a.z + step.z});
    b = fx::toShort({.x = b.x + step.x, .y = b.y + step.y, .z = b.z + step.z});
  }
}

// battle_effect.c: BTL_renderWireframeBox: centre, half extents, colour. The box's eight corners,
// turned by the instance's angles (RotMatrixZYX), its twelve edges as four LINE_F4 (three segments
// each) placed from the screen's centre whatever the drawing offset, opaque at entry 0x22.
void EffectRunner::wireframeBox(Slot& slot, const std::vector<std::int32_t>& args) {
  const EffectMatrix turn =
      fx::rotZYX(fx::toShort(vectorAt(slot, instanceAddress(RotationOffset))));
  const EffectVector centre = fx::toShort(vectorAt(slot, args[0]));
  const EffectVector extent = fx::toShort(vectorAt(slot, args[1]));
  const EffectVector color = vectorAt(slot, args[2]);
  std::array<EffectScreenPoint, BoxSigns.size()> corners{};
  for (std::size_t i = 0; i < BoxSigns.size(); ++i) {
    const EffectVector& sign = BoxSigns.at(i);
    const EffectVector turned = fx::toShort(fx::apply(turn, {.x = int16(extent.x * sign.x),
                                                             .y = int16(extent.y * sign.y),
                                                             .z = int16(extent.z * sign.z)}));
    corners.at(i) =
        project(fx::toShort(
                    {.x = turned.x + centre.x, .y = turned.y + centre.y, .z = turned.z + centre.z}))
            .first;
  }
  const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
  for (const auto& strip : BoxLines) {
    for (std::size_t k = 1; k < strip.size(); ++k) {
      draws_.push_back(
          {.slot = index,
           .shape = EffectScreenLine{
               .from = corners.at(strip.at(k - 1)),
               .to = corners.at(strip.at(k)),
               .color = {.x = color.x & ByteMask, .y = color.y & ByteMask, .z = color.z & ByteMask},
               .order = BoxOrder}});
    }
  }
}

// battle_effect.c: BTL_renderParallaxSprites: sprites, offset x and y. Each sprite of the list
// (up to a negative size) moves by the offset times its depth, so the deep ones move faster and
// show larger; opaque, with the texture's second palette row.
void EffectRunner::parallaxSprites(Slot& slot, const std::vector<std::int32_t>& args) {
  const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
  for (std::int32_t at = args[0];; at += ParallaxSpriteBytes) {
    const std::int32_t size = read(slot, at, EffectWidth::Int16);
    if (size < 0 || at + ParallaxSpriteBytes > static_cast<std::int32_t>(slot.data.size())) {
      break;
    }
    const std::int32_t px = read(slot, at + Int16Bytes, EffectWidth::Int16);
    const std::int32_t py = read(slot, at + (2 * Int16Bytes), EffectWidth::Int16);
    const std::int32_t depth = read(slot, at + (3 * Int16Bytes), EffectWidth::Int16);
    const std::int32_t u = read(slot, at + (4 * Int16Bytes), EffectWidth::Int16);
    // The place is an int16_t before it wraps.
    auto x = static_cast<std::int16_t>((depth * (px + args[1])) >> ParallaxShift);
    auto y = static_cast<std::int16_t>((depth * (py + args[2])) >> ParallaxShift);
    x = static_cast<std::int16_t>(x % ParallaxWrapX);
    y = static_cast<std::int16_t>(y % ParallaxWrapY);
    if (x < 0) {
      x = static_cast<std::int16_t>(x + ParallaxBelowX);
    }
    if (y < 0) {
      y = static_cast<std::int16_t>(y + ParallaxBelowY);
    }
    const auto side = static_cast<std::int16_t>((size * depth) >> ParallaxSizeShift);
    EffectScreenQuad quad;
    quad.corners =
        rectangle(x - ParallaxLeft + view_.centre.x, y - ParallaxUp + view_.centre.y, side, side);
    quad.u = u;
    quad.texelWidth = TileTexels;
    quad.texelHeight = TileTexels;
    quad.clutRow = ParallaxClutRow;
    quad.order = ScreenLayerOrder;
    draws_.push_back({.slot = index, .shape = quad});
  }
}

} // namespace dwe::game::battle
