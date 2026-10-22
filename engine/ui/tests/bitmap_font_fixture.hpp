#pragma once

#include <dwe/assets/image.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dwe::ui::fixture {

// A small bitmap font laid out like the one dwx extracts (docs/formats/textos.md): cells of 6 x 11
// in one row, a full-width character with its ASCII equivalent per glyph, and one glyph with no
// ASCII equivalent.
inline constexpr int CellWidth = 6;
inline constexpr int CellHeight = 11;
inline constexpr int Baseline = 10;
inline constexpr int Cells = 3;

inline constexpr std::string_view Meta = R"yaml(cell_width: 6
cell_height: 11
columns: 16
fallback: 2
colors:
- "0000"
- "6B5A"
glyphs:
- index: 0
  sjis: "8260"
  character: "Ａ"
  ascii: "A"
  x: 0
  y: 0
  width: 3
- index: 1
  sjis: "8261"
  character: "Ｂ"
  ascii: "B"
  x: 6
  y: 0
  width: 4
- index: 2
  sjis: "817B"
  character: "○"
  ascii: null
  x: 12
  y: 0
  width: 6
)yaml";

// The atlas: in every cell, ink in the first column from row `glyph + 2` down to the row above the
// baseline, so each glyph's ink starts at a different row.
inline assets::Image atlas() {
  assets::Image image{.width = CellWidth * Cells, .height = CellHeight, .pixels = {}};
  image.pixels.resize(std::size_t{image.width} * image.height * 4);
  for (int glyph = 0; glyph < Cells; ++glyph) {
    for (int row = glyph + 2; row < Baseline; ++row) {
      const std::size_t at = ((static_cast<std::size_t>(row) * image.width) +
                              static_cast<std::size_t>(glyph * CellWidth)) *
                             4;
      for (std::size_t channel = 0; channel < 4; ++channel) {
        image.pixels[at + channel] = std::byte{0xFF};
      }
    }
  }
  return image;
}

} // namespace dwe::ui::fixture
