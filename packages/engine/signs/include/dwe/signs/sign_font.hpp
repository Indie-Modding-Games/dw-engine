#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace dwe::signs {

// How much of each pixel a shape covers, 0 to 1, row by row.
struct Coverage {
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::vector<float> values;

  [[nodiscard]] float at(std::int32_t x, std::int32_t y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return 0.0F;
    }
    return values[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
                  static_cast<std::size_t>(x)];
  }
};

// A line of text as a font draws it: its coverage and where its pen started, in pixels of the
// coverage, so lines can be stacked by their baselines.
struct TextLine {
  Coverage ink;
  // The pen's start on the baseline, from the coverage's top left; the ink may start left of it.
  float originX = 0.0F;
  float baseline = 0.0F;
  // How far the pen moved (the line's advance).
  float advance = 0.0F;
};

// A TrueType or OpenType font (the packs' ui/fonts/*.ttf) drawn to coverage, without hinting, so
// the shapes scale smoothly.
class SignFont {
public:
  static core::Result<SignFont> load(std::vector<std::byte> fontFile);

  SignFont(SignFont&&) noexcept;
  SignFont& operator=(SignFont&&) noexcept;
  SignFont(const SignFont&) = delete;
  SignFont& operator=(const SignFont&) = delete;
  ~SignFont();

  // A line of UTF-8 text at `pixelSize` pixels per em, with `letterSpacing` extra pixels after
  // each letter. Letters the font lacks are left out.
  [[nodiscard]] TextLine drawLine(std::string_view text, float pixelSize,
                                  float letterSpacing) const;
  // The distance between baselines at `pixelSize`.
  [[nodiscard]] float lineHeight(float pixelSize) const;

private:
  struct Face;
  explicit SignFont(std::unique_ptr<Face> face);
  std::unique_ptr<Face> face_;
};

// The code points of UTF-8 text; invalid bytes become U+FFFD.
std::vector<char32_t> decodeUtf8(std::string_view text);

} // namespace dwe::signs
