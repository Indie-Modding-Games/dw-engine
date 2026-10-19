#pragma once

#include <dwe/assets/image.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <cstddef>
#include <span>
#include <vector>

#include "cook.hpp"

namespace dwe::dwx {

// Builds the full mip chain of an image, largest first: level 0 is the image itself and each next level halves both
// sides, rounding down and never below 1, down to 1x1. A texel of a level is the box-filtered average of the area it
// covers in the level above, weighted by how much of each texel it covers. Colour is averaged in linear light when
// srgb is set, and weighted by alpha, so transparent texels do not darken their neighbours.
std::vector<assets::Image> buildMipChain(const assets::Image& image, bool srgb);

// Decides how a PNG cooks. It is copied as is when it holds lookup data, which a KTX2 of RGBA colours would lose:
// when the PNG is indexed (colour type 3: the indices and the palette are kept, so the engine can still swap
// palettes), or when its sidecar says png: indexed or has a clut_rect (dwx extract writes both for TIM images with a
// CLUT). Otherwise it is stored in the sidecar's color_space, srgb or linear, sRGB when the sidecar does not say.
// Fails on a sidecar that is not a mapping or a color_space that is neither.
core::Result<CookOptions> planTexture(std::span<const std::byte> png, const core::YamlNode* sidecar);

// Cooks a PNG into an uncompressed RGBA8 KTX2 texture with its full mip chain, in the options' colour space.
core::Result<std::vector<std::byte>> cookTexture(std::span<const std::byte> png, const CookOptions& options);

} // namespace dwe::dwx
