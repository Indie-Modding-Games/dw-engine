#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ui/skin.hpp>

#include <cstdint>
#include <string>

namespace dwe::ui {

// The RCSS `@spritesheet` of a skin's sprite sheet over an image of the size the packs provide. An image that is the
// original size times a whole number (an upscaled copy) gets its rectangles times that number and a resolution that
// keeps every sprite at dpPerPixel dp per original pixel. Fails for an image of any other size.
core::Result<std::string> spriteSheetRcss(const SkinSpriteSheet& sheet, std::uint32_t imageWidth,
                                          std::uint32_t imageHeight);

} // namespace dwe::ui
