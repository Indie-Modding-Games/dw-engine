#pragma once

#include <dwe/assets/indexed_image.hpp>
#include <dwe/core/error.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace dwe::assets {

// What an extracted image's metadata document is named after: texture/stddat/time ->
// texture/stddat/time_meta.
inline constexpr std::string_view ImageMetaSuffix = "_meta";

// One palette as the metadata writes it: colours of the original's 16-bit format in 4 hexadecimal
// digits, separated by spaces ("0421 2129 4e73"), STP bit included (docs/formats/extract.md).
core::Result<std::vector<std::uint16_t>> parseOriginalPalette(std::string_view colors);

// The palettes the metadata of an extracted indexed image lists (`palettes`, one per CLUT row of
// the original texture), in the original's colours. Its PNG holds palette 0.
core::Result<std::vector<std::vector<std::uint16_t>>> parseImagePalettes(std::string_view metaYaml);

// A colour of the original's format as the extracted PNGs hold it: 0x0000 is transparent,
// everything else opaque, because the STP bit only matters to whoever draws semi-transparent
// primitives (docs/formats/tim.md).
PaletteColor imageColorFromOriginal(std::uint16_t value);

} // namespace dwe::assets
