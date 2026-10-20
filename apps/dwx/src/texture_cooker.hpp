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

// Decides how a PNG cooks. An indexed PNG (colour type 3) is copied as is, indices and palette, when its sidecar
// shows that the game chooses its palette while it runs: the sidecar lists more than one palette (the CLUT has
// several rows for the same pixels), or it is a map TIM with a CLUT (source ends in .MAP), whose sprites each choose
// a row. Every other PNG, indexed or not, is cooked as the colours it shows, in the sidecar's color_space, srgb or
// linear, sRGB when the sidecar does not say. Fails on a sidecar that is not a mapping or a color_space that is
// neither.
core::Result<CookOptions> planTexture(std::span<const std::byte> png, const core::YamlNode* sidecar);

// Cooks a PNG into a KTX2 texture with its full mip chain, in the options' colour space: Basis Universal UASTC (level
// 2, no RDO) supercompressed with zstd, as docs/formats/ktx2.md describes. The same PNG and options always give the
// same bytes with the pinned libktx. Safe to call from several threads.
core::Result<std::vector<std::byte>> cookTexture(std::span<const std::byte> png, const CookOptions& options);

} // namespace dwe::dwx
