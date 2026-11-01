#pragma once

#include <dwe/assets/image.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::app {

// anim.c: animateEntityTexture: the fire and bird species flicker their flames by copying one of
// three strips of their texture over its top strip. Each strip is FlameStripRows rows of the first
// `width` texels; the three frames lie below the top one, frame n at rows 32 (n + 1).
inline constexpr std::uint32_t FlameStripRows = 32;

// The width in texels of the flame strip of the species whose model is `model`, an asset id whose
// second part names the species' folder ("<species folder>/mera/model"); none for a species without
// flames. The original's widths are in VRAM
// words of its 4-bit images (0x1e, 0x28, 0x10, 0x2c), four texels each.
[[nodiscard]] std::optional<std::uint32_t> flameStripWidth(std::string_view model);

// The frame shown on the original's frame `frame` (PLAYTIME_FRAMES): on even frames, 2 when it is
// a multiple of 6, else 1 when a multiple of 4, else 0; none on odd frames, which keep the last.
[[nodiscard]] std::optional<std::uint32_t> flameStripFrame(std::uint64_t frame);

// The RGBA8 pixels of strip `strip` (0 to 2) of an image, `width` texels by FlameStripRows; empty
// when the image is too small for it.
[[nodiscard]] std::vector<std::byte> flameStripPixels(const assets::Image& image,
                                                      std::uint32_t width, std::uint32_t strip);

} // namespace dwe::app
