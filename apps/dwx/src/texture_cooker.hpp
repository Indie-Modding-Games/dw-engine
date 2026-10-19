#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/core/error.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace dwe::dwx {

// Builds the full mip chain of an image, largest first: level 0 is the image itself and each next level halves both
// sides, rounding down and never below 1, down to 1x1. A texel of a level is the box-filtered average of the area it
// covers in the level above, weighted by how much of each texel it covers. Colour is averaged in linear light when
// srgb is set, and weighted by alpha, so transparent texels do not darken their neighbours.
std::vector<assets::Image> buildMipChain(const assets::Image& image, bool srgb);

// Returns true if an asset's texture holds colour, stored as sRGB. Ids whose last segment ends in _depth, _mask or
// _normal hold other data and are stored linear.
bool isColorTexture(const assets::AssetId& id);

// Cooks a PNG into an uncompressed RGBA8 KTX2 texture with its full mip chain.
core::Result<std::vector<std::byte>> cookTexture(std::span<const std::byte> png, const assets::AssetId& id);

} // namespace dwe::dwx
