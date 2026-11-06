#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// The player's own images (docs/guides/imagenes-personalizadas.md): a pack of Layer::User whose
// PNGs replace how the game's images are drawn, at their size or a whole number of times larger.
// Geometry never changes: every position, rectangle and size stays in the original image's pixels,
// and a replacement only adds detail.
namespace dwe::assets {

// The largest factor a replacement may be larger by, the same on both axes.
inline constexpr std::uint32_t MaxReplacementScale = 8;

// How many times larger `width` x `height` is than the original image, when it is a whole number
// from 1 to MaxReplacementScale and the same on both axes (the same aspect ratio); none otherwise.
std::optional<std::uint32_t> replacementScale(std::uint32_t originalWidth,
                                              std::uint32_t originalHeight, std::uint32_t width,
                                              std::uint32_t height);

// What names a replacement of one palette of an indexed image, which the game draws with any of
// its palettes: texture/etcdat/fi_info drawn with palette 12 is texture/etcdat/fi_info_palette12.
inline constexpr std::string_view PaletteImageSuffix = "_palette";

// The id of the replacement of `image` drawn with `palette`.
core::Result<AssetId> paletteImageId(const AssetId& image, std::size_t palette);

} // namespace dwe::assets
