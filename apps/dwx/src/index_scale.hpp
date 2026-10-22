#pragma once

#include <dwe/assets/indexed_image.hpp>
#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::dwx {

// How index_scale grows an image (docs/architecture/remaster-pipeline.md, «index_scale»).
enum class IndexScaleMode : std::uint8_t {
    // The classic pixel-art neighbour rules: each pixel splits into n x n by which neighbours share its index. It
    // never mixes colours, so the result stays indexed and every palette of the image still applies.
    Edge,
    // Each pixel repeated n x n times.
    Nearest,
};

inline constexpr std::uint32_t MinIndexScaleFactor = 1;
inline constexpr std::uint32_t MaxIndexScaleFactor = 8;

// The mode a recipe names (edge, nearest), if it is one.
std::optional<IndexScaleMode> indexScaleModeNamed(std::string_view name);

// Whether a mode can grow an image by a factor: nearest by any from 1 to 8, edge by 1, 2, 3, 4, 6 and 8 (the 2x and
// 3x rules composed in a fixed order, 2 first; 5 and 7 have no rules).
bool supportsIndexScale(IndexScaleMode mode, std::uint32_t factor);

// The image grown by a whole factor in palette-index space: the same palette, every index taken from the source. The
// result depends only on the indices, the factor and the mode, the same on every platform.
core::Result<assets::IndexedImage> indexScale(const assets::IndexedImage& image, std::uint32_t factor,
                                              IndexScaleMode mode);

} // namespace dwe::dwx
