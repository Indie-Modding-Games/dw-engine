#pragma once

#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/map_images.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dwe::render {

// The CPU half of HD scenery's best quality (docs/architecture/remaster-pipeline.md, «Nitidez»):
// the field's prerendered background, and the models' textures, doubled
// in size by an edge-directed interpolation written for this engine. Each new pixel is a cubic
// interpolation along the direction in which the image changes least (the two diagonals first,
// then the rows and the columns, as in directional cubic convolution), or a blend of both
// directions where neither dominates, so edges come out as clean lines instead of stairs and soft
// shading stays soft. Before that, an optional pass melts the original's low-contrast dithering
// (2-colour checkerboards, single-pixel speckle) into the shade it stands for, leaving every change
// above a small contrast as it is, and after it a light contrast-adaptive sharpen.
//
// The decisions (which direction, how much each texel weighs) are taken once, on a guidance image,
// and applied in the same way to every channel, so an upscaled background is a fixed linear mix of
// the original's texels: each palette variant upscaled on its own and the fades between them mixed
// afterwards give what upscaling the mixed palette would (to the anti-ringing clamp and the
// rounding). Texels of different classes (alpha 0, STP, opaque) never mix, and each new pixel has
// the class of the source texel up and to the left of it.

// Pixels of a float image with `channels` values each, row-major.
struct FloatImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t channels = 0;
  std::vector<float> values;

  [[nodiscard]] float* at(std::uint32_t x, std::uint32_t y) {
    return values.data() + ((static_cast<std::size_t>(y) * width) + x) * channels;
  }
  [[nodiscard]] const float* at(std::uint32_t x, std::uint32_t y) const {
    return values.data() + ((static_cast<std::size_t>(y) * width) + x) * channels;
  }
};

struct UpscaleOptions {
  // The largest guidance difference (0 to 255, luma plus half the chroma) the de-dithering treats
  // as dither or noise; 0 turns it off.
  float dither = 10.0F;
  // How much the doubled image is sharpened (0 off, 1 strongest): a contrast-adaptive sharpen that
  // leaves edges without halos.
  float sharpen = 0.35F;
};

// The image the decisions are taken on and the classes that may not mix, one per texel.
struct UpscaleGuide {
  // Three values per texel: luma and the two chroma differences, 0 to 255.
  FloatImage guidance;
  std::vector<std::uint8_t> classes;
};

// Doubles `image`: the result is 2 * width by 2 * height, its pixel (2x, 2y) the source's (x, y)
// (centres aligned: the result's pixel u sits at the source's u / 2), the last row and column
// extrapolated from their neighbours. The guide must be the image's size; it is doubled too, so the
// result can be doubled again.
struct Doubled {
  FloatImage image;
  UpscaleGuide guide;
};
[[nodiscard]] Doubled upscaleDouble(const FloatImage& image, const UpscaleGuide& guide,
                                    const UpscaleOptions& options);

// The guide of colours (RGBA bytes, 4 per texel): YCbCr of the colours, classes from alpha (0,
// below 192, the rest), or, with alphaAsClass false, a single class.
[[nodiscard]] UpscaleGuide guideFromColors(std::uint32_t width, std::uint32_t height,
                                           std::span<const assets::PaletteColor> colors,
                                           bool alphaAsClass);

// A map's background: one image of indices and its palette variants (the time of day).
struct UpscaledScenery {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // How many of its texels per texel of the background, and where the background's texel u falls in
  // it: at u * scale + shift. The engine's doubling puts u at 2u (shift 0); an image whose squares
  // of scale x scale texels cover the background's texels (a player's image,
  // MapBackgroundReplacement) puts it at the middle of its square, shift (scale - 1) / 2.
  std::uint32_t scale = 2;
  float shift = 0.0F;
  // Per variant, width * height colours. Alpha is the class of the nearest source texel as the
  // palette gives it (the GPU takes it from the original pixels anyway).
  std::vector<std::vector<assets::PaletteColor>> variants;
};

// Doubles a background in every palette variant. The decisions are taken on the first variant's
// colours (each variant shades the same picture).
// A player's images of a background as HD scenery draws them: each variant as it is, its squares of
// scale x scale texels over the background's texels.
[[nodiscard]] UpscaledScenery
sceneryFromReplacement(const assets::MapBackgroundReplacement& replacement);

[[nodiscard]] UpscaledScenery
upscaleScenery(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> indices,
               std::span<const std::vector<assets::PaletteColor>> palettes,
               const UpscaleOptions& options = {});

// A texture's RGBA pixels (4 bytes each) scaled by 2^doublings: transparent texels (alpha 0) take
// the colour of their opaque neighbours first so no dark fringe comes in, and alpha is
// interpolated like a colour, so a cut-off draws a smooth outline. Returns the new size's pixels.
[[nodiscard]] std::vector<std::byte> upscaleRgba(std::uint32_t width, std::uint32_t height,
                                                 std::span<const std::byte> pixels,
                                                 std::uint32_t doublings);

} // namespace dwe::render
