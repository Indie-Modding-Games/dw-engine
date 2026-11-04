#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

namespace dwe::render {

// HD scenery's target wraps around as the camera scrolls (MapRenderer::hdSceneryScene): these say
// what a frame filters into it.

// The window's pixels to filter, as rectangles (x, y, end x, end y), for a window `size` pixels
// big whose scroll moved by `moved` pixels since the target was filled: all of them when it was
// not, or moved a whole window; else the strips at the edges the scroll uncovered (none when it
// did not move).
[[nodiscard]] inline std::vector<glm::ivec4> hdUncoveredRegions(glm::ivec2 size, glm::ivec2 moved,
                                                                bool filled) {
  std::vector<glm::ivec4> regions;
  if (!filled || std::abs(moved.x) >= size.x || std::abs(moved.y) >= size.y) {
    regions.emplace_back(0, 0, size.x, size.y);
    return regions;
  }
  if (moved.x != 0) {
    regions.emplace_back(moved.x > 0 ? size.x - moved.x : 0, 0, moved.x > 0 ? size.x : -moved.x,
                         size.y);
  }
  if (moved.y != 0) {
    regions.emplace_back(0, moved.y > 0 ? size.y - moved.y : 0, size.x,
                         moved.y > 0 ? size.y : -moved.y);
  }
  return regions;
}

// Pixels [begin, end) of the scaled background (end - begin at most `length`) where a target
// `length` pixels long keeps them, at begin modulo length: up to two pieces, each (its first pixel
// in the background, its first pixel in the target, how many); the second is empty when the span
// does not wrap.
[[nodiscard]] inline std::array<glm::ivec3, 2> hdWrappedSpan(int begin, int end, int length) {
  const int wrapped = ((begin % length) + length) % length;
  const int first = std::min(end - begin, length - wrapped);
  return {glm::ivec3{begin, wrapped, first}, glm::ivec3{begin + first, 0, end - begin - first}};
}

} // namespace dwe::render
