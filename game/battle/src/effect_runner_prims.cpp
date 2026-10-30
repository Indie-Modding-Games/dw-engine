#include <dwe/game/battle/effect_runner.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "effect_math.hpp"

// What the technique effects draw as the original's own GPU primitives rather than sprites or
// models: the layers on the screen (battle_effect.c: BTL_renderScrollingBackground,
// BTL_renderParallaxSprites) and the ribbons of light (BTL_renderRibbonStrip), as EffectRunner
// members.
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
  } else if (name == "get_camera_rotation" && !args.empty()) {
    // battle_effect.c: BTL_getCameraRotation: the angles (matrixToEuler2) of the camera's
    // view-to-world rotation.
    setVector(slot, args[0], fx::toShort(fx::eulerYXZ(fx::transposed(view_.camera))));
  } else {
    return false;
  }
  return true;
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
  // A wider screen than the original's shows more of the same tiles (none extra at 320 x 240).
  const std::int32_t extraColumns = extraTiles(view_.screen.x / 2, OriginalScreen.x / 2);
  const std::int32_t extraRows = extraTiles(view_.screen.y / 2, OriginalScreen.y / 2);
  const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
  for (std::int32_t row = -extraRows; row < TileRows + extraRows; ++row) {
    const std::int32_t y = (sy & TileMask) - TilesUp + (row * TileSize);
    const std::int32_t tileRow = wrapped(ty + row, TileRows) * TileColumns;
    for (std::int32_t column = -extraColumns; column < TileColumns + extraColumns; ++column) {
      const std::int32_t x = (sx & TileMask) - TilesLeft + (column * TileSize);
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
    quad.corners = rectangle(x - ParallaxLeft, y - ParallaxUp, side, side);
    quad.u = u;
    quad.texelWidth = TileTexels;
    quad.texelHeight = TileTexels;
    quad.clutRow = ParallaxClutRow;
    quad.order = ScreenLayerOrder;
    draws_.push_back({.slot = index, .shape = quad});
  }
}

} // namespace dwe::game::battle
