#include <dwe/render/scenery_upscale.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <thread>
#include <utility>

namespace dwe::render {

namespace {

// How much stronger the change across one direction must be than across the other for the
// interpolation to follow only the other one (directional cubic convolution's 115 %).
constexpr float DirectionRatio = 1.15F;
// The exponent of the blend between both directions where neither dominates: the direction that
// changes less weighs (1 + d)^-Exponent.
constexpr float BlendExponent = 5.0F;
// Guidance channels after the image's own.
constexpr std::uint32_t GuidanceChannels = 3;
// Rows below which the work is not split among threads.
constexpr std::uint32_t RowsPerThread = 64;

// The cubic convolution weights at distances 1.5, 0.5, 0.5 and 1.5: (-1, 9, 9, -1) / 16.
constexpr std::array<float, 4> CubicWeights{-1.0F / 16.0F, 9.0F / 16.0F, 9.0F / 16.0F,
                                            -1.0F / 16.0F};

// The image with its guidance appended to each pixel, and the classes.
struct Work {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // Values per pixel: the image's, then the three of the guidance.
  std::uint32_t stride = 0;
  std::uint32_t channels = 0;
  std::vector<float> values;
  std::vector<std::uint8_t> classes;

  [[nodiscard]] const float* at(std::int64_t x, std::int64_t y) const {
    const auto cx = static_cast<std::size_t>(std::clamp<std::int64_t>(x, 0, width - 1));
    const auto cy = static_cast<std::size_t>(std::clamp<std::int64_t>(y, 0, height - 1));
    return values.data() + (cy * width + cx) * stride;
  }
  [[nodiscard]] float* atMutable(std::uint32_t x, std::uint32_t y) {
    return values.data() + ((static_cast<std::size_t>(y) * width) + x) * stride;
  }
  [[nodiscard]] std::uint8_t classAt(std::int64_t x, std::int64_t y) const {
    const auto cx = static_cast<std::size_t>(std::clamp<std::int64_t>(x, 0, width - 1));
    const auto cy = static_cast<std::size_t>(std::clamp<std::int64_t>(y, 0, height - 1));
    return classes[cy * width + cx];
  }
  [[nodiscard]] float guideDistance(const float* a, const float* b) const {
    const float* ga = a + channels;
    const float* gb = b + channels;
    return std::abs(ga[0] - gb[0]) + (0.5F * (std::abs(ga[1] - gb[1]) + std::abs(ga[2] - gb[2])));
  }
};

// Runs body(firstRow, endRow) over [0, rows) on a few threads.
template <typename Body>
void forRows(std::uint32_t rows, Body&& body) {
  const std::uint32_t hardware = std::max(1U, std::thread::hardware_concurrency());
  const std::uint32_t threads = std::clamp(rows / RowsPerThread, 1U, std::min(hardware, 8U));
  if (threads <= 1) {
    body(0U, rows);
    return;
  }
  std::vector<std::jthread> workers;
  workers.reserve(threads);
  for (std::uint32_t thread = 0; thread < threads; ++thread) {
    const std::uint32_t first = rows * thread / threads;
    const std::uint32_t end = rows * (thread + 1) / threads;
    workers.emplace_back([&body, first, end] { body(first, end); });
  }
}

// Melts low-contrast dither and noise: each texel becomes the weighted mean (1 2 1 / 2 4 2 / 1 2 1)
// of the texels around it of its class whose guidance differs from its own by at most `limit`.
void dedither(Work& work, float limit) {
  const Work source = work;
  forRows(work.height, [&](std::uint32_t firstRow, std::uint32_t endRow) {
    std::vector<float> sum(work.stride);
    for (std::uint32_t y = firstRow; y < endRow; ++y) {
      for (std::uint32_t x = 0; x < work.width; ++x) {
        const float* centre = source.at(x, y);
        const std::uint8_t kind = source.classAt(x, y);
        std::ranges::fill(sum, 0.0F);
        float total = 0.0F;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const std::int64_t nx = static_cast<std::int64_t>(x) + dx;
            const std::int64_t ny = static_cast<std::int64_t>(y) + dy;
            if (nx < 0 || ny < 0 || nx >= work.width || ny >= work.height) {
              continue;
            }
            const float* other = source.at(nx, ny);
            if (source.classAt(nx, ny) != kind || source.guideDistance(centre, other) > limit) {
              continue;
            }
            const float weight = static_cast<float>((2 - std::abs(dx)) * (2 - std::abs(dy)));
            for (std::uint32_t channel = 0; channel < work.stride; ++channel) {
              sum[channel] += weight * other[channel];
            }
            total += weight;
          }
        }
        float* out = work.atMutable(x, y);
        for (std::uint32_t channel = 0; channel < work.stride; ++channel) {
          out[channel] = sum[channel] / total;
        }
      }
    }
  });
}

// A light adaptive sharpen of the doubled image, in the manner of contrast-adaptive sharpening:
// each pixel moves away from its four neighbours (those of its class) by an amount that shrinks
// where the guidance's luma is already near black or white or has strong contrast, so soft
// gradients gain definition and edges do not ring. The amount is decided on the guidance and
// applied alike to every channel (fades stay a linear mix of the variants).
void sharpen(Work& work, float strength) {
  const Work source = work;
  // The neighbours' weight at full amount: 0 to -1/5 (contrast-adaptive sharpening's strongest).
  const float peak = -0.2F * std::clamp(strength, 0.0F, 1.0F);
  constexpr std::array<std::array<int, 2>, 4> Cross{{{0, -1}, {-1, 0}, {1, 0}, {0, 1}}};
  forRows(work.height, [&](std::uint32_t firstRow, std::uint32_t endRow) {
    for (std::uint32_t y = firstRow; y < endRow; ++y) {
      for (std::uint32_t x = 0; x < work.width; ++x) {
        const float* centre = source.at(x, y);
        const std::uint8_t kind = source.classAt(x, y);
        if (kind == 0) {
          continue;
        }
        std::array<const float*, 4> around{};
        float low = centre[work.channels] / 255.0F;
        float high = low;
        for (std::size_t side = 0; side < Cross.size(); ++side) {
          const std::int64_t nx = static_cast<std::int64_t>(x) + Cross[side][0];
          const std::int64_t ny = static_cast<std::int64_t>(y) + Cross[side][1];
          around[side] = source.classAt(nx, ny) == kind ? source.at(nx, ny) : centre;
          const float luma = around[side][work.channels] / 255.0F;
          low = std::min(low, luma);
          high = std::max(high, luma);
        }
        if (high <= 0.0F) {
          continue;
        }
        const float amount = std::sqrt(std::clamp(std::min(low, 1.0F - high) / high, 0.0F, 1.0F));
        const float weight = amount * peak;
        const float total = 1.0F + (4.0F * weight);
        const auto sharpened = [&](std::uint32_t channel) {
          float sum = centre[channel];
          for (const float* other : around) {
            sum += weight * other[channel];
          }
          return sum / total;
        };
        // How much of the change is kept: all of it unless the luma would leave the range of its
        // neighbours (a halo); then only what reaches the range's end.
        const float luma = centre[work.channels];
        const float change = sharpened(work.channels) - luma;
        float keep = 1.0F;
        if (change > 0.0F && luma + change > high * 255.0F) {
          keep = std::max(0.0F, (high * 255.0F) - luma) / change;
        } else if (change < 0.0F && luma + change < low * 255.0F) {
          keep = std::min(0.0F, (low * 255.0F) - luma) / change;
        }
        float* out = work.atMutable(x, y);
        for (std::uint32_t channel = 0; channel < work.stride; ++channel) {
          out[channel] = centre[channel] + (keep * (sharpened(channel) - centre[channel]));
        }
      }
    }
  });
}

// Four taps along one direction, nearest two in the middle, and the class they agree on.
struct Taps {
  std::array<const float*, 4> values;
  std::array<std::uint8_t, 4> classes;
};

// The cubic interpolation of the taps into out, clamped to the middle two (no ringing). Outer taps
// of another class than their middle neighbour are replaced by it; middle taps of different
// classes do not mix (the first one is taken). Returns the class of the result.
std::uint8_t interpolate(const Taps& taps, std::uint32_t stride, float* out) {
  if (taps.classes[1] != taps.classes[2]) {
    std::memcpy(out, taps.values[1], stride * sizeof(float));
    return taps.classes[1];
  }
  const float* first = taps.classes[0] == taps.classes[1] ? taps.values[0] : taps.values[1];
  const float* last = taps.classes[3] == taps.classes[2] ? taps.values[3] : taps.values[2];
  for (std::uint32_t channel = 0; channel < stride; ++channel) {
    const float b = taps.values[1][channel];
    const float c = taps.values[2][channel];
    const float value = (CubicWeights[0] * first[channel]) + (CubicWeights[1] * b) +
                        (CubicWeights[2] * c) + (CubicWeights[3] * last[channel]);
    out[channel] = std::clamp(value, std::min(b, c), std::max(b, c));
  }
  return taps.classes[1];
}

// One new pixel from two candidate directions and how much the image changes across each (dOne,
// dTwo: a large change across a direction means an edge along the other one).
std::uint8_t directional(const Taps& one, const Taps& two, float changeOne, float changeTwo,
                         std::uint32_t stride, float* out, std::vector<float>& scratch) {
  if ((1.0F + changeOne) > DirectionRatio * (1.0F + changeTwo)) {
    return interpolate(two, stride, out);
  }
  if ((1.0F + changeTwo) > DirectionRatio * (1.0F + changeOne)) {
    return interpolate(one, stride, out);
  }
  scratch.resize(stride);
  const std::uint8_t kind = interpolate(one, stride, out);
  if (interpolate(two, stride, scratch.data()) != kind) {
    return kind;
  }
  // Scaled down so the powers stay in range; only their ratio matters.
  const float weightOne = 1.0F / (1.0F + std::pow(changeOne / 16.0F, BlendExponent));
  const float weightTwo = 1.0F / (1.0F + std::pow(changeTwo / 16.0F, BlendExponent));
  const float total = weightOne + weightTwo;
  for (std::uint32_t channel = 0; channel < stride; ++channel) {
    out[channel] = ((weightOne * out[channel]) + (weightTwo * scratch[channel])) / total;
  }
  return kind;
}

Work doubled(const Work& source) {
  Work result;
  result.width = source.width * 2;
  result.height = source.height * 2;
  result.stride = source.stride;
  result.channels = source.channels;
  result.values.assign(static_cast<std::size_t>(result.width) * result.height * result.stride,
                       0.0F);
  result.classes.assign(static_cast<std::size_t>(result.width) * result.height, 0);
  const std::uint32_t stride = source.stride;
  const auto classIndex = [&result](std::uint32_t x, std::uint32_t y) {
    return (static_cast<std::size_t>(y) * result.width) + x;
  };

  // A new pixel's class is the one of the source texel up and to the left of it (u / 2, v / 2), as
  // the GPU's nearest texel and upscaleScenery's alpha take it: where two classes meet and the
  // interpolation took the other one, the pixel is that texel's.
  const auto settle = [&](std::uint32_t u, std::uint32_t v, std::uint8_t kind) {
    const std::uint8_t own = source.classAt(u / 2, v / 2);
    if (kind != own) {
      std::memcpy(result.atMutable(u, v), source.at(u / 2, v / 2), stride * sizeof(float));
    }
    result.classes[classIndex(u, v)] = own;
  };

  // The source's texels at the even pixels, and the diagonal centres.
  forRows(source.height, [&](std::uint32_t firstRow, std::uint32_t endRow) {
    std::vector<float> scratch;
    for (std::uint32_t y = firstRow; y < endRow; ++y) {
      for (std::uint32_t x = 0; x < source.width; ++x) {
        std::memcpy(result.atMutable(2 * x, 2 * y), source.at(x, y), stride * sizeof(float));
        result.classes[classIndex(2 * x, 2 * y)] = source.classAt(x, y);

        const std::int64_t ix = x;
        const std::int64_t iy = y;
        // Change across the "/" diagonal and across the "\" one, over the 4 x 4 texels around.
        float slash = 0.0F;
        float backslash = 0.0F;
        for (std::int64_t i = ix - 1; i <= ix + 1; ++i) {
          for (std::int64_t j = iy; j <= iy + 2; ++j) {
            slash += source.guideDistance(source.at(i, j), source.at(i + 1, j - 1));
          }
          for (std::int64_t j = iy - 1; j <= iy + 1; ++j) {
            backslash += source.guideDistance(source.at(i, j), source.at(i + 1, j + 1));
          }
        }
        const Taps alongSlash{.values = {source.at(ix + 2, iy - 1), source.at(ix + 1, iy),
                                         source.at(ix, iy + 1), source.at(ix - 1, iy + 2)},
                              .classes = {source.classAt(ix + 2, iy - 1),
                                          source.classAt(ix + 1, iy), source.classAt(ix, iy + 1),
                                          source.classAt(ix - 1, iy + 2)}};
        const Taps alongBackslash{
            .values = {source.at(ix - 1, iy - 1), source.at(ix, iy), source.at(ix + 1, iy + 1),
                       source.at(ix + 2, iy + 2)},
            .classes = {source.classAt(ix - 1, iy - 1), source.classAt(ix, iy),
                        source.classAt(ix + 1, iy + 1), source.classAt(ix + 2, iy + 2)}};
        settle((2 * x) + 1, (2 * y) + 1,
               directional(alongSlash, alongBackslash, slash, backslash, stride,
                           result.atMutable((2 * x) + 1, (2 * y) + 1), scratch));
      }
    }
  });

  // The pixels between two source texels of a row or a column, from the known pixels around them.
  // Past an edge a tap steps back by two pixels at a time, so it stays a known pixel (one of the
  // first step's) instead of one of this step's, not made yet.
  const auto inside = [](std::int64_t coordinate, std::uint32_t size) {
    while (coordinate < 0) {
      coordinate += 2;
    }
    while (coordinate >= static_cast<std::int64_t>(size)) {
      coordinate -= 2;
    }
    return coordinate;
  };
  const auto known = [&result, &inside](std::int64_t u, std::int64_t v) {
    return result.at(inside(u, result.width), inside(v, result.height));
  };
  const auto knownClass = [&result, &inside](std::int64_t u, std::int64_t v) {
    return result.classAt(inside(u, result.width), inside(v, result.height));
  };
  forRows(result.height, [&](std::uint32_t firstRow, std::uint32_t endRow) {
    std::vector<float> scratch;
    for (std::uint32_t v = firstRow; v < endRow; ++v) {
      for (std::uint32_t u = (v + 1) % 2; u < result.width; u += 2) {
        const std::int64_t iu = u;
        const std::int64_t iv = v;
        const auto change = [&](std::int64_t du, std::int64_t dv) {
          // Along (du, dv): three steps on the pixel's own line, two on each neighbouring one.
          const std::int64_t pu = dv;
          const std::int64_t pv = du;
          float sum = 0.0F;
          for (std::int64_t k = -3; k <= 1; k += 2) {
            sum += result.guideDistance(known(iu + (k * du), iv + (k * dv)),
                                        known(iu + ((k + 2) * du), iv + ((k + 2) * dv)));
          }
          for (const std::int64_t side : {-1, 1}) {
            for (std::int64_t k = -2; k <= 0; k += 2) {
              sum += result.guideDistance(
                  known(iu + (side * pu) + (k * du), iv + (side * pv) + (k * dv)),
                  known(iu + (side * pu) + ((k + 2) * du), iv + (side * pv) + ((k + 2) * dv)));
            }
          }
          return sum;
        };
        const auto taps = [&](std::int64_t du, std::int64_t dv) {
          Taps line{};
          for (std::int64_t k = 0; k < 4; ++k) {
            const std::int64_t offset = (2 * k) - 3;
            line.values[static_cast<std::size_t>(k)] =
                known(iu + (offset * du), iv + (offset * dv));
            line.classes[static_cast<std::size_t>(k)] =
                knownClass(iu + (offset * du), iv + (offset * dv));
          }
          return line;
        };
        settle(u, v,
               directional(taps(1, 0), taps(0, 1), change(1, 0), change(0, 1), stride,
                           result.atMutable(u, v), scratch));
      }
    }
  });
  return result;
}

Work workOf(const FloatImage& image, const UpscaleGuide& guide) {
  Work work;
  work.width = image.width;
  work.height = image.height;
  work.channels = image.channels;
  work.stride = image.channels + GuidanceChannels;
  const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
  work.values.resize(pixels * work.stride);
  for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
    std::memcpy(work.values.data() + (pixel * work.stride),
                image.values.data() + (pixel * image.channels), image.channels * sizeof(float));
    std::memcpy(work.values.data() + (pixel * work.stride) + image.channels,
                guide.guidance.values.data() + (pixel * GuidanceChannels),
                GuidanceChannels * sizeof(float));
  }
  work.classes = guide.classes;
  return work;
}

Doubled split(Work&& work) {
  Doubled out;
  const std::size_t pixels = static_cast<std::size_t>(work.width) * work.height;
  out.image = {.width = work.width, .height = work.height, .channels = work.channels, .values = {}};
  out.image.values.resize(pixels * work.channels);
  out.guide.guidance = {
      .width = work.width, .height = work.height, .channels = GuidanceChannels, .values = {}};
  out.guide.guidance.values.resize(pixels * GuidanceChannels);
  for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
    std::memcpy(out.image.values.data() + (pixel * work.channels),
                work.values.data() + (pixel * work.stride), work.channels * sizeof(float));
    std::memcpy(out.guide.guidance.values.data() + (pixel * GuidanceChannels),
                work.values.data() + (pixel * work.stride) + work.channels,
                GuidanceChannels * sizeof(float));
  }
  out.guide.classes = std::move(work.classes);
  return out;
}

std::array<float, 3> yuv(float red, float green, float blue) {
  const float luma = (0.299F * red) + (0.587F * green) + (0.114F * blue);
  return {luma, 0.564F * (blue - luma), 0.713F * (red - luma)};
}

std::uint8_t toByte(float value) {
  return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 255.0F)));
}

} // namespace

Doubled upscaleDouble(const FloatImage& image, const UpscaleGuide& guide,
                      const UpscaleOptions& options) {
  Work work = workOf(image, guide);
  if (options.dither > 0.0F) {
    dedither(work, options.dither);
  }
  Work result = doubled(work);
  if (options.sharpen > 0.0F) {
    sharpen(result, options.sharpen);
  }
  return split(std::move(result));
}

UpscaleGuide guideFromColors(std::uint32_t width, std::uint32_t height,
                             std::span<const assets::PaletteColor> colors, bool alphaAsClass) {
  UpscaleGuide guide;
  guide.guidance = {.width = width, .height = height, .channels = GuidanceChannels, .values = {}};
  const std::size_t pixels = static_cast<std::size_t>(width) * height;
  guide.guidance.values.resize(pixels * GuidanceChannels);
  guide.classes.resize(pixels);
  for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
    const assets::PaletteColor colour = colors[pixel];
    const auto [luma, blue, red] = yuv(colour.red, colour.green, colour.blue);
    float* out = guide.guidance.values.data() + (pixel * GuidanceChannels);
    out[0] = luma;
    out[1] = blue;
    out[2] = red;
    std::uint8_t kind = 0;
    if (alphaAsClass) {
      kind = colour.alpha == 0 ? 0 : (colour.alpha < 192 ? 1 : 2);
    }
    guide.classes[pixel] = kind;
  }
  return guide;
}

UpscaledScenery sceneryFromReplacement(const assets::MapBackgroundReplacement& replacement) {
  UpscaledScenery scenery{.width = 0,
                          .height = 0,
                          .scale = replacement.scale,
                          .shift = (static_cast<float>(replacement.scale) - 1.0F) / 2.0F,
                          .variants = {}};
  for (const assets::Image& image : replacement.variants) {
    scenery.width = image.width;
    scenery.height = image.height;
    std::vector<assets::PaletteColor>& texels =
        scenery.variants.emplace_back(image.pixels.size() / sizeof(assets::PaletteColor));
    std::memcpy(texels.data(), image.pixels.data(), texels.size() * sizeof(assets::PaletteColor));
  }
  return scenery;
}

UpscaledScenery upscaleScenery(std::uint32_t width, std::uint32_t height,
                               std::span<const std::uint8_t> indices,
                               std::span<const std::vector<assets::PaletteColor>> palettes,
                               const UpscaleOptions& options) {
  UpscaledScenery result{
      .width = width * 2, .height = height * 2, .scale = 2, .shift = 0.0F, .variants = {}};
  const std::size_t pixels = static_cast<std::size_t>(width) * height;
  if (palettes.empty() || indices.size() != pixels || pixels == 0) {
    return {};
  }
  const auto variants = static_cast<std::uint32_t>(palettes.size());
  FloatImage image{.width = width, .height = height, .channels = variants * 3, .values = {}};
  image.values.resize(pixels * image.channels);
  std::vector<assets::PaletteColor> first(pixels);
  for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
    const std::uint8_t index = indices[pixel];
    first[pixel] = palettes[0][index];
    for (std::uint32_t variant = 0; variant < variants; ++variant) {
      const assets::PaletteColor colour = palettes[variant][index];
      float* out = image.values.data() + (pixel * image.channels) + (variant * 3);
      out[0] = colour.red;
      out[1] = colour.green;
      out[2] = colour.blue;
    }
  }
  const Doubled doubled =
      upscaleDouble(image, guideFromColors(width, height, first, true), options);
  result.variants.resize(variants);
  for (std::uint32_t variant = 0; variant < variants; ++variant) {
    std::vector<assets::PaletteColor>& out = result.variants[variant];
    out.resize(static_cast<std::size_t>(result.width) * result.height);
    for (std::uint32_t v = 0; v < result.height; ++v) {
      for (std::uint32_t u = 0; u < result.width; ++u) {
        const float* value = doubled.image.at(u, v) + (variant * 3);
        const std::uint8_t index = indices[(static_cast<std::size_t>(v / 2) * width) + (u / 2)];
        out[(static_cast<std::size_t>(v) * result.width) + u] = {
            .red = toByte(value[0]),
            .green = toByte(value[1]),
            .blue = toByte(value[2]),
            .alpha = palettes[variant][index].alpha};
      }
    }
  }
  return result;
}

std::vector<std::byte> upscaleRgba(std::uint32_t width, std::uint32_t height,
                                   std::span<const std::byte> pixels, std::uint32_t doublings) {
  const std::size_t count = static_cast<std::size_t>(width) * height;
  if (pixels.size() != count * 4 || count == 0) {
    return {pixels.begin(), pixels.end()};
  }
  std::vector<assets::PaletteColor> colours(count);
  std::memcpy(colours.data(), pixels.data(), pixels.size());
  // Transparent texels take the mean colour of their drawn neighbours, a ring at a time, so the
  // interpolation brings no black in from them.
  std::vector<bool> filled(count);
  std::size_t missing = 0;
  for (std::size_t texel = 0; texel < count; ++texel) {
    filled[texel] = colours[texel].alpha != 0;
    missing += filled[texel] ? 0 : 1;
  }
  for (int ring = 0; ring < 8 && missing > 0 && missing < count; ++ring) {
    std::vector<bool> next = filled;
    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        const std::size_t texel = (static_cast<std::size_t>(y) * width) + x;
        if (filled[texel]) {
          continue;
        }
        std::array<int, 3> sum{};
        int found = 0;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const std::int64_t nx = static_cast<std::int64_t>(x) + dx;
            const std::int64_t ny = static_cast<std::int64_t>(y) + dy;
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
              continue;
            }
            const std::size_t other =
                (static_cast<std::size_t>(ny) * width) + static_cast<std::size_t>(nx);
            if (filled[other]) {
              sum[0] += colours[other].red;
              sum[1] += colours[other].green;
              sum[2] += colours[other].blue;
              ++found;
            }
          }
        }
        if (found > 0) {
          colours[texel].red = static_cast<std::uint8_t>(sum[0] / found);
          colours[texel].green = static_cast<std::uint8_t>(sum[1] / found);
          colours[texel].blue = static_cast<std::uint8_t>(sum[2] / found);
          next[texel] = true;
          --missing;
        }
      }
    }
    filled = std::move(next);
  }

  FloatImage image{.width = width, .height = height, .channels = 4, .values = {}};
  image.values.resize(count * 4);
  std::vector<assets::PaletteColor> shown(count);
  for (std::size_t texel = 0; texel < count; ++texel) {
    const assets::PaletteColor colour = colours[texel];
    float* out = image.values.data() + (texel * 4);
    out[0] = colour.red;
    out[1] = colour.green;
    out[2] = colour.blue;
    out[3] = colour.alpha;
    // The outline is an edge for the guidance: transparent texels guide as black.
    const float cover = static_cast<float>(colour.alpha) / 255.0F;
    shown[texel] = {.red = toByte(colour.red * cover),
                    .green = toByte(colour.green * cover),
                    .blue = toByte(colour.blue * cover),
                    .alpha = 255};
  }
  UpscaleGuide guide = guideFromColors(width, height, shown, false);
  for (std::uint32_t step = 0; step < doublings; ++step) {
    Doubled next = upscaleDouble(image, guide, {.dither = 0.0F, .sharpen = 0.0F});
    image = std::move(next.image);
    guide = std::move(next.guide);
  }
  std::vector<std::byte> result(image.values.size());
  for (std::size_t value = 0; value < image.values.size(); ++value) {
    result[value] = static_cast<std::byte>(toByte(image.values[value]));
  }
  return result;
}

} // namespace dwe::render
