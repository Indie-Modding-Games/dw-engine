#pragma once

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dwe::debug {

// Shapes a game draws over its picture with the developer overlay (DebugUi::setShapes), such as
// the field map's collision and exits. Points are fractions of the window: (0, 0) its top-left
// corner, (1, 1) its bottom-right one, so they need not know the window's size or scale. They are
// drawn under the overlay's windows, whether those are shown or not.

// A colour as Dear ImGui packs it: red in the low byte, alpha in the high one.
using OverlayColor = std::uint32_t;

constexpr OverlayColor overlayColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                    std::uint8_t alpha = 255) {
  return static_cast<OverlayColor>(red) | (static_cast<OverlayColor>(green) << 8U) |
         (static_cast<OverlayColor>(blue) << 16U) | (static_cast<OverlayColor>(alpha) << 24U);
}

// A four-sided polygon, filled and outlined (an alpha of 0 leaves either out).
struct OverlayQuad {
  std::array<glm::vec2, 4> corners{};
  OverlayColor fill = 0;
  OverlayColor outline = 0;
};

struct OverlayLine {
  glm::vec2 from{0.0F};
  glm::vec2 to{0.0F};
  OverlayColor color = 0;
  float thickness = 1.0F;
};

// A filled circle of `radius` window pixels.
struct OverlayDot {
  glm::vec2 at{0.0F};
  float radius = 3.0F;
  OverlayColor color = 0;
};

// A text whose top-left corner is at `at`, on a dark box so it reads over any picture.
struct OverlayLabel {
  glm::vec2 at{0.0F};
  std::string text;
  OverlayColor color = 0;
};

struct OverlayShapes {
  std::vector<OverlayQuad> quads;
  std::vector<OverlayLine> lines;
  std::vector<OverlayDot> dots;
  std::vector<OverlayLabel> labels;

  [[nodiscard]] bool empty() const {
    return quads.empty() && lines.empty() && dots.empty() && labels.empty();
  }
  void clear() {
    quads.clear();
    lines.clear();
    dots.clear();
    labels.clear();
  }
};

} // namespace dwe::debug
