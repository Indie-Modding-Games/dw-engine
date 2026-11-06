#include <dwe/ui/world_map.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace dwe::ui {
namespace {

// How far the land reaches past an area's radius, as a multiple of it.
constexpr float LandReach = 1.75F;
// The sharpness of the smooth union of the areas' land: higher joins them with tighter necks.
constexpr float UnionSharpness = 9.0F;
// How much the noise moves the coast (in units of reach) and the borders (in canvas units).
constexpr float CoastWobble = 0.24F;
constexpr float BorderWarp = 15.0F;
// The size of the noise's features, in canvas units.
constexpr float CoastScale = 42.0F;
constexpr float WarpScale = 70.0F;
constexpr float GrainScale = 9.0F;

struct Rgb {
  float r = 0.0F;
  float g = 0.0F;
  float b = 0.0F;
};

Rgb rgbOf(std::uint32_t rgba) {
  constexpr float Max = 255.0F;
  constexpr std::uint32_t Byte = 0xFF;
  return {.r = static_cast<float>((rgba >> 24U) & Byte) / Max,
          .g = static_cast<float>((rgba >> 16U) & Byte) / Max,
          .b = static_cast<float>((rgba >> 8U) & Byte) / Max};
}

Rgb mix(Rgb a, Rgb b, float t) {
  return {.r = a.r + ((b.r - a.r) * t), .g = a.g + ((b.g - a.g) * t), .b = a.b + ((b.b - a.b) * t)};
}

Rgb scale(Rgb c, float f) {
  return {.r = c.r * f, .g = c.g * f, .b = c.b * f};
}

void put(assets::Image& image, std::size_t index, Rgb c) {
  constexpr float Max = 255.0F;
  const auto byteOf = [](float v) {
    return static_cast<std::byte>(std::lround(std::clamp(v, 0.0F, 1.0F) * Max));
  };
  const std::size_t offset = index * 4;
  image.pixels[offset] = byteOf(c.r);
  image.pixels[offset + 1] = byteOf(c.g);
  image.pixels[offset + 2] = byteOf(c.b);
  image.pixels[offset + 3] = std::byte{0xFF};
}

Rgb get(const assets::Image& image, std::size_t index) {
  constexpr float Max = 255.0F;
  const std::size_t offset = index * 4;
  return {.r = static_cast<float>(image.pixels[offset]) / Max,
          .g = static_cast<float>(image.pixels[offset + 1]) / Max,
          .b = static_cast<float>(image.pixels[offset + 2]) / Max};
}

float smoothstep(float edge0, float edge1, float x) {
  const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
  return t * t * (3.0F - (2.0F * t));
}

// A hash of a lattice point to [0, 1).
float hash(std::int32_t x, std::int32_t y, std::uint32_t seed) {
  std::uint32_t h = (static_cast<std::uint32_t>(x) * 374761393U) +
                    (static_cast<std::uint32_t>(y) * 668265263U) + (seed * 2246822519U);
  h = (h ^ (h >> 13U)) * 1274126177U;
  h ^= h >> 16U;
  constexpr float Range = 16777216.0F;
  return static_cast<float>(h & 0xFFFFFFU) / Range;
}

// Smooth value noise in [0, 1).
float valueNoise(float x, float y, std::uint32_t seed) {
  const float fx = std::floor(x);
  const float fy = std::floor(y);
  const auto ix = static_cast<std::int32_t>(fx);
  const auto iy = static_cast<std::int32_t>(fy);
  const float tx = smoothstep(0.0F, 1.0F, x - fx);
  const float ty = smoothstep(0.0F, 1.0F, y - fy);
  const float top = hash(ix, iy, seed) + ((hash(ix + 1, iy, seed) - hash(ix, iy, seed)) * tx);
  const float bottom =
      hash(ix, iy + 1, seed) + ((hash(ix + 1, iy + 1, seed) - hash(ix, iy + 1, seed)) * tx);
  return top + ((bottom - top) * ty);
}

// Three octaves of value noise, centred on 0 (about -0.5 to 0.5).
float fractalNoise(float x, float y, std::uint32_t seed) {
  float sum = 0.0F;
  float amplitude = 0.5F;
  float frequency = 1.0F;
  float total = 0.0F;
  for (std::uint32_t octave = 0; octave < 3; ++octave) {
    sum += amplitude * valueNoise(x * frequency, y * frequency, seed + octave);
    total += amplitude;
    amplitude *= 0.5F;
    frequency *= 2.0F;
  }
  return (sum / total) - 0.5F;
}

// How far into the land a point lies: above 0 on land, below 0 at sea, about 1 at an area's centre.
float landAt(const WorldMapPicture& picture, float x, float y, std::uint32_t seed) {
  float sum = 0.0F;
  for (const WorldMapArea& area : picture.areas) {
    const float reach = std::max(area.radius, 1.0F) * LandReach;
    const float distance = std::hypot(x - area.x, y - area.y);
    sum += std::exp(UnionSharpness * (1.0F - (distance / reach)));
  }
  if (sum <= 0.0F) {
    return -1.0F;
  }
  const float land = std::log(sum) / UnionSharpness;
  return land + (CoastWobble * fractalNoise(x / CoastScale, y / CoastScale, seed));
}

std::size_t nearestArea(const WorldMapPicture& picture, float x, float y, std::uint32_t seed) {
  const float wx = x + (BorderWarp * fractalNoise(x / WarpScale, y / WarpScale, seed + 11U));
  const float wy = y + (BorderWarp * fractalNoise(x / WarpScale, y / WarpScale, seed + 23U));
  std::size_t best = 0;
  float bestScore = std::numeric_limits<float>::max();
  for (std::size_t i = 0; i < picture.areas.size(); ++i) {
    const WorldMapArea& area = picture.areas[i];
    const float score = std::hypot(wx - area.x, wy - area.y) / std::max(area.radius, 1.0F);
    if (score < bestScore) {
      bestScore = score;
      best = i;
    }
  }
  return best;
}

void drawRoad(assets::Image& image, const std::vector<std::int32_t>& regions, float x0, float y0,
              float x1, float y1, float width, Rgb colour) {
  const float length = std::hypot(x1 - x0, y1 - y0);
  if (length < 1.0F) {
    return;
  }
  // Dashes of 10 pixels with gaps of 7, in steps of half a pixel.
  constexpr float Dash = 10.0F;
  constexpr float Period = 17.0F;
  const auto steps = static_cast<std::int32_t>(length * 2.0F);
  const auto half = static_cast<std::int32_t>(std::ceil(width));
  for (std::int32_t step = 0; step <= steps; ++step) {
    const float along = static_cast<float>(step) * 0.5F;
    if (std::fmod(along, Period) >= Dash) {
      continue;
    }
    const float t = along / length;
    const float cx = x0 + ((x1 - x0) * t);
    const float cy = y0 + ((y1 - y0) * t);
    for (std::int32_t dy = -half; dy <= half; ++dy) {
      for (std::int32_t dx = -half; dx <= half; ++dx) {
        const auto px = static_cast<std::int32_t>(cx) + dx;
        const auto py = static_cast<std::int32_t>(cy) + dy;
        if (px < 0 || py < 0 || std::cmp_greater_equal(px, image.width) ||
            std::cmp_greater_equal(py, image.height) ||
            std::hypot(static_cast<float>(px) + 0.5F - cx, static_cast<float>(py) + 0.5F - cy) >
                width) {
          continue;
        }
        const std::size_t index =
            (static_cast<std::size_t>(py) * image.width) + static_cast<std::size_t>(px);
        if (regions[index] < 0) {
          continue;
        }
        put(image, index, mix(get(image, index), colour, 0.7F));
      }
    }
  }
}

} // namespace

std::optional<std::size_t> worldMapAreaAt(const WorldMapPicture& picture, float x, float y,
                                          std::uint32_t seed) {
  if (picture.areas.empty() || landAt(picture, x, y, seed) <= 0.0F) {
    return std::nullopt;
  }
  return nearestArea(picture, x, y, seed);
}

assets::Image paintWorldMap(const WorldMapPicture& picture, const WorldMapStyle& style) {
  assets::Image image;
  if (picture.areas.empty() || picture.width <= 0.0F || picture.height <= 0.0F ||
      style.pixelsWide == 0) {
    return image;
  }
  const float unitsPerPixel = picture.width / static_cast<float>(style.pixelsWide);
  image.width = style.pixelsWide;
  image.height = std::max<std::uint32_t>(
      1, static_cast<std::uint32_t>(std::lround(picture.height / unitsPerPixel)));
  const std::size_t count = static_cast<std::size_t>(image.width) * image.height;
  image.pixels.resize(count * 4);

  // First the land and the area of every pixel, then the colours, which look at the neighbours.
  std::vector<float> land(count);
  std::vector<std::int32_t> regions(count, -1);
  for (std::uint32_t py = 0; py < image.height; ++py) {
    for (std::uint32_t px = 0; px < image.width; ++px) {
      const float x = (static_cast<float>(px) + 0.5F) * unitsPerPixel;
      const float y = (static_cast<float>(py) + 0.5F) * unitsPerPixel;
      const std::size_t index = (static_cast<std::size_t>(py) * image.width) + px;
      land[index] = landAt(picture, x, y, style.seed);
      if (land[index] > 0.0F) {
        regions[index] = static_cast<std::int32_t>(nearestArea(picture, x, y, style.seed));
      }
    }
  }

  const Rgb deep = rgbOf(style.deepSea);
  const Rgb shallow = rgbOf(style.shallowSea);
  const Rgb coast = rgbOf(style.coast);
  const Rgb border = rgbOf(style.border);
  const Rgb unknown = rgbOf(style.unknown);
  std::vector<Rgb> colours;
  colours.reserve(picture.areas.size());
  for (const WorldMapArea& area : picture.areas) {
    const Rgb colour = rgbOf(area.colour);
    colours.push_back(area.known ? colour : mix(colour, unknown, style.unknownFade));
  }
  // The coast's ink is about two pixels wide whatever the image's size.
  const auto coastReach =
      static_cast<std::int32_t>(std::max(1.0F, static_cast<float>(style.pixelsWide) / 700.0F));
  const auto regionAt = [&](std::int64_t px, std::int64_t py) {
    if (px < 0 || py < 0 || px >= image.width || py >= image.height) {
      return -1;
    }
    return regions[(static_cast<std::size_t>(py) * image.width) + static_cast<std::size_t>(px)];
  };
  for (std::uint32_t py = 0; py < image.height; ++py) {
    for (std::uint32_t px = 0; px < image.width; ++px) {
      const std::size_t index = (static_cast<std::size_t>(py) * image.width) + px;
      const float x = (static_cast<float>(px) + 0.5F) * unitsPerPixel;
      const float y = (static_cast<float>(py) + 0.5F) * unitsPerPixel;
      const std::int32_t region = regions[index];
      bool nearOther = false;
      for (std::int32_t dy = -coastReach; dy <= coastReach && !nearOther; ++dy) {
        for (std::int32_t dx = -coastReach; dx <= coastReach && !nearOther; ++dx) {
          nearOther = (regionAt(std::int64_t{px} + dx, std::int64_t{py} + dy) < 0) != (region < 0);
        }
      }
      Rgb colour;
      if (region < 0) {
        // The shallows fade into the deep sea away from the coast, with faint ripples.
        const float depth = smoothstep(0.0F, 0.45F, -land[index]);
        colour = mix(shallow, deep, depth);
        const float ripple =
            std::sin((y / 6.0F) + (9.0F * fractalNoise(x / 40.0F, y / 40.0F, style.seed + 5U)));
        if (ripple > 0.96F && depth > 0.35F) {
          colour = mix(colour, shallow, 0.2F);
        }
        if (nearOther) {
          colour = coast;
        }
      } else {
        const float grain = fractalNoise(x / GrainScale, y / GrainScale, style.seed + 31U);
        const float relief = fractalNoise(x / 30.0F, y / 30.0F, style.seed + 41U);
        colour = scale(colours[static_cast<std::size_t>(region)],
                       0.94F + (0.10F * grain) + (0.12F * relief));
        const bool edge = regionAt(std::int64_t{px} + 1, py) != region ||
                          regionAt(px, std::int64_t{py} + 1) != region;
        const bool dot = ((px + py) / 3U) % 2U == 0U;
        if (edge && dot && !nearOther) {
          colour = border;
        }
        if (nearOther) {
          colour = coast;
        }
      }
      // A parchment vignette towards the edges.
      const float u = (static_cast<float>(px) / static_cast<float>(image.width)) - 0.5F;
      const float v = (static_cast<float>(py) / static_cast<float>(image.height)) - 0.5F;
      const float edgeness = std::max(std::abs(u), std::abs(v)) * 2.0F;
      colour = scale(colour, 1.0F - (0.22F * smoothstep(0.7F, 1.0F, edgeness)));
      put(image, index, colour);
    }
  }

  const Rgb road = rgbOf(style.road);
  const float roadWidth = std::max(1.0F, static_cast<float>(style.pixelsWide) / 650.0F);
  for (const WorldMapRoad& link : picture.roads) {
    if (link.from >= picture.areas.size() || link.to >= picture.areas.size()) {
      continue;
    }
    const WorldMapArea& from = picture.areas[link.from];
    const WorldMapArea& to = picture.areas[link.to];
    const Rgb colour = from.known && to.known ? road : mix(road, unknown, 0.5F);
    drawRoad(image, regions, from.x / unitsPerPixel, from.y / unitsPerPixel, to.x / unitsPerPixel,
             to.y / unitsPerPixel, roadWidth, colour);
  }
  return image;
}

WorldMapPoint locateOnWorldMap(const WorldMapPicture& picture, float x, float y) {
  if (picture.width <= 0.0F || picture.height <= 0.0F) {
    return {};
  }
  return {.u = std::clamp(x / picture.width, 0.0F, 1.0F),
          .v = std::clamp(y / picture.height, 0.0F, 1.0F)};
}

} // namespace dwe::ui
