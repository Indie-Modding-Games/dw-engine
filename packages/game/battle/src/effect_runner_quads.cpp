#include <dwe/game/battle/effect_runner.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "effect_math.hpp"

// The technique effects' own textured quads (battle_effect.c: BTL_render2DTexturedQuad,
// BTL_render3DTexturedQuad), the glowing tube of rings (BTL_renderRingTube) and the waves round a
// point (BTL_renderRadialWaves), the view-space helper (BTL_convertToViewSpace) and the map's
// layer (BTL_disableMapLayer), as EffectRunner members.
namespace dwe::game::battle {

namespace {

namespace fx = effect_math;

using assets::EffectWidth;

constexpr std::int32_t ByteMask = 0xFF;
constexpr std::int32_t ShortMask = 0xFFFF;
constexpr std::int32_t Int32Bytes = 4;
// A POLY_FT4's tpage: the semi-transparency rate in bits 5 and 6.
constexpr std::int32_t RateShift = 5;
constexpr std::int32_t RateMask = 3;
// The ordering table's entries the effects may use (0x20 < entry < 0x1000), the view depth's
// 16 units each.
constexpr std::int32_t NearestEntry = 0x20;
constexpr std::int32_t Entries = 0x1000;
constexpr std::int32_t OrderShift = 4;

// BTL_renderRingTube: 10 points a ring (512 per turn for _cos and _sin, from 0x80), the rings'
// texels 0x17 x 0x1f, every other ring 24 texels to the right, added.
constexpr std::int32_t TubePoints = 10;
constexpr std::int32_t TubeTurn = 0x200;
constexpr std::int32_t TubeStart = 0x80;
constexpr std::int32_t TubeTexelWidth = 0x17;
constexpr std::int32_t TubeTexelHeight = 0x1F;
constexpr std::int32_t TubeOtherU = 24;
constexpr std::int32_t FixedShift = 12;

// BTL_renderRadialWaves: 6 rays a turn apart by 0x2aa (the first already turned), each quads from
// radius 0xc8 out to 0xbb8 by 0x64; a quad's height eases 0xc8 -> 0xfa -> 0x7d -> 0xa over the
// radius, rolled by rsin(phase + radius * 4 + ray * 0x309) with the phase -400 a frame; its half
// width radius / 5. Texels from BTL_RADIAL_WAVE_UVS (the first quad, the middle ones and the last
// ones from 0xb54), palette row 1, colour 0x80 (1), added.
constexpr std::int32_t WaveRays = 6;
constexpr std::int32_t WaveRayTurn = 0x2AA;
constexpr std::int32_t WaveFirst = 0xC8;
constexpr std::int32_t WaveEnd = 0xBB8;
constexpr std::int32_t WaveStep = 0x64;
constexpr std::int32_t WaveRise = 0x2EE;
constexpr std::int32_t WaveFall = 0x8CA;
constexpr std::int32_t WaveHeightStart = 0xC8;
constexpr std::int32_t WaveHeightTop = 0xFA;
constexpr std::int32_t WaveHeightMiddle = 0x7D;
constexpr std::int32_t WaveHeightEnd = 0xA;
constexpr std::int32_t WavePhasePerFrame = -400;
constexpr std::int32_t WaveRadiusPhase = 4;
constexpr std::int32_t WaveRayPhase = 0x309;
constexpr std::int32_t WaveWidthDivisor = 5;
constexpr std::int32_t WaveFirstQuad = 0x12C;
constexpr std::int32_t WaveLastQuads = 0xB54;
constexpr std::int32_t WaveColor = 0x80;
constexpr std::int32_t WaveClutRow = 1;

// EfeInstance: the frame, the transform's position, its rotation.
constexpr std::int32_t PositionOffset = 4;
constexpr std::int32_t RotationOffset = 0x10;

// A quad's texels as BTL_RADIAL_WAVE_UVS give them, as (u, v, width, height) and the order its
// four projected corners take (the table runs u along the radius and v across it).
struct WaveTexels {
  std::int32_t u;
  std::int32_t v;
  std::int32_t width;
  std::int32_t height;
  std::array<std::size_t, 4> corners;
};
constexpr WaveTexels WaveMiddle{
    .u = 0, .v = 0x10, .width = 7, .height = 0xF, .corners = {2, 0, 3, 1}};
constexpr WaveTexels WaveFirstTexels{
    .u = 8, .v = 0, .width = 0xF, .height = 0xF, .corners = {0, 2, 1, 3}};
constexpr WaveTexels WaveLast{.u = 8, .v = 0, .width = 0xF, .height = 0xF, .corners = {2, 0, 3, 1}};

// The blend of a POLY_FT4 whose tpage the scripts ORed `semi` into (0 draws it opaque).
EffectBlend blendOfPage(std::int32_t semi) {
  if (semi == 0) {
    return EffectBlend::Opaque;
  }
  return static_cast<EffectBlend>((semi >> RateShift) & RateMask);
}

// A POLY_FT4's 16-bit coordinates.
std::int32_t int16(std::int32_t value) {
  return static_cast<std::int16_t>(value);
}

// The ordering-table entry of a view depth, if the effects draw there.
bool drawnAt(std::int32_t order) {
  return order > NearestEntry && order < Entries;
}

// The colour bytes of an EfeColor.
EffectVector colorBytes(const EffectVector& color) {
  return {.x = color.x & ByteMask, .y = color.y & ByteMask, .z = color.z & ByteMask};
}

} // namespace

bool EffectRunner::quadOp(Slot& slot, std::string_view name,
                          const std::vector<std::int32_t>& args) {
  const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
  if (name == "render_2d_quad" && args.size() >= 12) {
    // BTL_render2DTexturedQuad: x, y, width, height (or, for a negative rate, four points), entry,
    // colour, u, v, texel width and height, palette row, rate.
    const std::int32_t order = args[4];
    if (!drawnAt(order)) {
      return true;
    }
    std::int32_t semi = args[11];
    EffectScreenQuad quad;
    if (semi < 0) {
      semi = -semi & ShortMask;
      for (std::size_t i = 0; i < quad.corners.size(); ++i) {
        quad.corners.at(i) = {.x = int16(read(slot, args[i], EffectWidth::Int32)),
                              .y = int16(read(slot, args[i] + Int32Bytes, EffectWidth::Int32))};
      }
    } else {
      const std::int32_t x = int16(args[0]);
      const std::int32_t y = int16(args[1]);
      const std::int32_t width = int16(args[2]);
      const std::int32_t height = int16(args[3]);
      quad.corners = {EffectScreenPoint{.x = x, .y = y}, EffectScreenPoint{.x = x + width, .y = y},
                      EffectScreenPoint{.x = x, .y = y + height},
                      EffectScreenPoint{.x = x + width, .y = y + height}};
    }
    for (EffectScreenPoint& corner : quad.corners) {
      corner.x += view_.centre.x;
      corner.y += view_.centre.y;
    }
    quad.color = colorBytes(vectorAt(slot, args[5]));
    quad.u = args[6];
    quad.v = args[7];
    quad.texelWidth = args[8];
    quad.texelHeight = args[9];
    quad.clutRow = args[10];
    quad.blend = blendOfPage(semi);
    quad.order = order;
    draws_.push_back({.slot = index, .shape = quad});
  } else if (name == "render_3d_quad" && args.size() >= 11) {
    // BTL_render3DTexturedQuad and effect_common.c: addScreenPolyFT4: four world points, colour,
    // u, v, texel width and height, palette row, rate; drawn at their average depth.
    EffectScreenQuad quad;
    std::int32_t depth = 0;
    for (std::size_t i = 0; i < quad.corners.size(); ++i) {
      const auto [pixel, z] = project(vectorAt(slot, args[i]));
      quad.corners.at(i) = {.x = pixel.x + view_.centre.x, .y = pixel.y + view_.centre.y};
      depth += z;
    }
    const std::int32_t order = (depth / 4) >> OrderShift;
    if (!drawnAt(order)) {
      return true;
    }
    quad.color = colorBytes(vectorAt(slot, args[4]));
    quad.u = args[5];
    quad.v = args[6];
    quad.texelWidth = args[7];
    quad.texelHeight = args[8];
    quad.clutRow = args[9];
    quad.blend = blendOfPage(args[10]);
    quad.order = order;
    draws_.push_back({.slot = index, .shape = quad});
  } else if (name == "render_ring_tube" && args.size() >= 6) {
    ringTube(slot, args);
  } else if (name == "render_radial_waves" && !args.empty()) {
    radialWaves(slot, args);
  } else if (name == "to_view_space" && args.size() >= 4) {
    // BTL_convertToViewSpace: out place, out angles, place, angles: the transform (RotMatrixYXZ)
    // seen from the camera (GsMulCoord0 with GsWSMATRIX), its angles by matrixToEuler2.
    const EffectMatrix camera = worldToScreen();
    const EffectVector at = fx::toShort(vectorAt(slot, args[2]));
    const EffectMatrix turn = fx::rotYXZ(fx::toShort(vectorAt(slot, args[3])));
    const EffectVector moved = fx::apply(camera, at);
    setVector(slot, args[0],
              fx::toShort(EffectVector{.x = moved.x + camera.t.x,
                                       .y = moved.y + camera.t.y,
                                       .z = moved.z + camera.t.z}));
    setVector(slot, args[1], fx::toShort(fx::eulerYXZ(fx::multiply(camera, turn))));
  } else if (name == "disable_map_layer") {
    mapHidden_ = true;
  } else if (name == "mark_finished") {
    // BTL_markEFEFinished clears an entry of a table nothing reads (UNUSED_EFE_ARRAY).
  } else {
    return false;
  }
  return true;
}

// battle_effect.c: BTL_renderRingTube: centre, ring count, radii, heights, grey levels (one each
// ring, 32-bit), the first ring's texels. Ten points round each ring at its height under the
// centre; a quad joins two points of a ring to the same two of the next, at the first point's
// depth, unless one of its points is off the drawn depths.
void EffectRunner::ringTube(Slot& slot, const std::vector<std::int32_t>& args) {
  const EffectVector centre = vectorAt(slot, args[0]);
  const std::int32_t rings = args[1];
  if (rings < 2) {
    return;
  }
  struct Point {
    EffectScreenPoint pixel;
    std::int32_t order = 0;
  };
  std::vector<Point> points;
  points.reserve(static_cast<std::size_t>(rings * TubePoints));
  for (std::int32_t i = 0; i < rings; ++i) {
    const std::int32_t radius = read(slot, args[2] + (i * Int32Bytes), EffectWidth::Int32);
    const std::int32_t height = read(slot, args[3] + (i * Int32Bytes), EffectWidth::Int32);
    for (std::int32_t j = 0; j < TubePoints; ++j) {
      const std::int32_t angle = TubeStart - (j * TubeTurn / TubePoints);
      const EffectVector at{.x = centre.x + ((radius * fx::cos512(angle)) >> FixedShift),
                            .y = centre.y + height,
                            .z = centre.z + ((radius * fx::sin512(angle)) >> FixedShift)};
      const auto [pixel, depth] = project(at);
      points.push_back(
          {.pixel = {.x = int16(pixel.x) + view_.centre.x, .y = int16(pixel.y) + view_.centre.y},
           .order = depth >> OrderShift});
    }
  }
  const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
  const auto point = [&](std::int32_t ring, std::int32_t j) -> const Point& {
    return points.at(static_cast<std::size_t>((ring * TubePoints) + (j % TubePoints)));
  };
  for (std::int32_t i = 0; i + 1 < rings; ++i) {
    const std::int32_t grey = read(slot, args[4] + (i * Int32Bytes), EffectWidth::Int32) & ByteMask;
    for (std::int32_t j = 0; j < TubePoints; ++j) {
      const std::array<const Point*, 4> corners{&point(i + 1, j + 1), &point(i, j + 1),
                                                &point(i + 1, j), &point(i, j)};
      const bool off = std::ranges::any_of(corners, [](const Point* p) {
        return p->order < NearestEntry + 1 || p->order >= Entries;
      });
      if (off) {
        continue;
      }
      EffectScreenQuad quad;
      for (std::size_t k = 0; k < corners.size(); ++k) {
        quad.corners.at(k) = corners.at(k)->pixel;
      }
      quad.u = ((i + args[5]) & 1) * TubeOtherU;
      quad.v = 0;
      quad.texelWidth = TubeTexelWidth;
      quad.texelHeight = TubeTexelHeight;
      quad.color = {.x = grey, .y = grey, .z = grey};
      quad.blend = EffectBlend::Add;
      quad.order = point(i, j).order;
      draws_.push_back({.slot = index, .shape = quad});
    }
  }
}

// battle_effect.c: BTL_renderRadialWaves: scale. The waves turn with the instance (RotMatrixYXZ)
// and sit at its place; a quad whose projection faults (the GTE's error flag) is not drawn.
void EffectRunner::radialWaves(Slot& slot, const std::vector<std::int32_t>& args) {
  const EffectVector scale = vectorAt(slot, args[0]);
  EffectVector turn = vectorAt(slot, instanceAddress(RotationOffset));
  const EffectVector at = vectorAt(slot, instanceAddress(PositionOffset));
  const std::int32_t phase = read(slot, instanceAddress(0), EffectWidth::Int32) * WavePhasePerFrame;
  const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
  for (std::int32_t ray = 0; ray < WaveRays; ++ray) {
    turn.y += WaveRayTurn;
    const EffectMatrix place = fx::scaled(fx::rotYXZ(fx::toShort(turn)), scale);
    const auto world = [&](const EffectVector& local) {
      const EffectVector moved = fx::apply(place, fx::toShort(local));
      return EffectVector{.x = moved.x + at.x, .y = moved.y + at.y, .z = moved.z + at.z};
    };
    std::int32_t previousHalf = 0;
    std::int32_t previousHeight = 0;
    std::int32_t previousRadius = 0;
    for (std::int32_t radius = WaveFirst; radius < WaveEnd; radius += WaveStep) {
      std::int32_t height =
          radius < WaveRise + 1
              ? fx::interpolateClamped(WaveFirst, WaveRise, radius, WaveHeightStart, WaveHeightTop)
          : radius < WaveFall + 1
              ? fx::interpolateClamped(WaveRise, WaveFall, radius, WaveHeightTop, WaveHeightMiddle)
              : fx::interpolateClamped(WaveFall, WaveEnd, radius, WaveHeightMiddle, WaveHeightEnd);
      const std::int32_t angle = phase + (radius * WaveRadiusPhase) + (ray * WaveRayPhase);
      height = ((height * fx::rsin(angle)) >> FixedShift) - height - WaveFirst;
      const std::int32_t half = radius / WaveWidthDivisor;
      if (radius != WaveFirst) {
        const std::array<EffectVector, 4> local{
            EffectVector{.x = -half, .y = height, .z = radius},
            EffectVector{.x = half, .y = height, .z = radius},
            EffectVector{.x = -previousHalf, .y = previousHeight, .z = previousRadius},
            EffectVector{.x = previousHalf, .y = previousHeight, .z = previousRadius}};
        std::array<Projection, 4> projected{};
        bool fault = false;
        std::int32_t depth = 0;
        for (std::size_t k = 0; k < local.size(); ++k) {
          projected.at(k) = projection(world(local.at(k)));
          fault = fault || projected.at(k).fault;
          depth += projected.at(k).depth;
        }
        const std::int32_t order = (depth / 4) >> OrderShift;
        if (!fault && order < Entries) {
          const WaveTexels& texels = radius == WaveFirstQuad   ? WaveFirstTexels
                                     : radius >= WaveLastQuads ? WaveLast
                                                               : WaveMiddle;
          EffectScreenQuad quad;
          for (std::size_t k = 0; k < quad.corners.size(); ++k) {
            const EffectScreenPoint& pixel = projected.at(texels.corners.at(k)).pixel;
            quad.corners.at(k) = {.x = pixel.x + view_.centre.x, .y = pixel.y + view_.centre.y};
          }
          quad.u = texels.u;
          quad.v = texels.v;
          quad.texelWidth = texels.width;
          quad.texelHeight = texels.height;
          quad.color = {.x = WaveColor, .y = WaveColor, .z = WaveColor};
          quad.clutRow = WaveClutRow;
          quad.blend = EffectBlend::Add;
          quad.order = order;
          draws_.push_back({.slot = index, .shape = quad});
        }
      }
      previousHalf = half;
      previousHeight = height;
      previousRadius = radius;
    }
  }
}

} // namespace dwe::game::battle
