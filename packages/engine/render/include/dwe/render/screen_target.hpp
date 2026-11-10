#pragma once

#include <dwe/rhi/types.hpp>

#include <algorithm>
#include <cstdint>

namespace dwe::render {

// The size a scene texture showing a whole screen of `shape` (the original's 320 x 240) is drawn
// at in a window of `window` pixels: the window's height, its width by the shape's aspect, never
// smaller than the shape. The UI draws such a screen at most the window's height tall, so each of
// its pixels covers at most one of the window's: sharp at any window size.
[[nodiscard]] constexpr rhi::Extent screenTargetExtent(rhi::Extent shape, rhi::Extent window) {
  if (shape.width == 0 || shape.height == 0) {
    return shape;
  }
  const std::uint64_t height = std::max(window.height, shape.height);
  const std::uint64_t width = ((height * shape.width) + (shape.height / 2)) / shape.height;
  return {.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height)};
}

} // namespace dwe::render
