#pragma once

#include <dwe/assets/image.hpp>
#include <dwe/core/error.hpp>

#include <cstddef>
#include <map>
#include <string_view>
#include <vector>

namespace dwe::ui {

// A glyph of a bitmap font: the top left of its cell in the atlas and how far it advances, in atlas
// pixels.
struct BitmapGlyph {
  int x = 0;
  int y = 0;
  int advance = 0;
};

// A piece of text drawn by one font: the bitmap font, or the fallback font for characters the
// bitmap font lacks.
struct TextRun {
  std::string_view text;
  bool bitmap = false;
};

// A bitmap font as dwx extracts the original's (docs/formats/textos.md): an atlas of equal cells,
// where ink is any pixel that is not transparent, and its glyphs by Unicode character.
class BitmapFont {
public:
  // Reads the font from its meta (font/main_meta.yaml) and atlas (font/main.png, decoded). A glyph
  // answers to its character and to the ASCII character it stands for ("Ａ" and "A"). The letters
  // and signs the original lacks (á, Ñ, ¿, «...) are composed from its own glyphs and drawn into
  // the atlas (composed_glyphs.cpp). Fails on a meta without the cell size or the glyph list, on a
  // glyph outside the atlas, and on a baseline outside a cell.
  static core::Result<BitmapFont> parse(std::string_view meta, assets::Image atlas, int baseline);

  // The glyph of a character, or nullptr when the font lacks it.
  [[nodiscard]] const BitmapGlyph* glyph(char32_t character) const;

  // Splits UTF-8 text into runs the font draws and runs it lacks, in order. Invalid UTF-8 goes to
  // the fallback.
  [[nodiscard]] std::vector<TextRun> splitRuns(std::string_view text) const;

  // The atlas with its ink as opaque white and the rest transparent, each pixel repeated `scale`
  // times in both directions so that it is drawn one texel per pixel (premultiplied RGBA).
  [[nodiscard]] assets::Image inkAtlas(int scale) const;

  // The first row of a glyph's cell with ink, or the cell height for a glyph without ink.
  [[nodiscard]] int inkTop(const BitmapGlyph& glyph) const;

  // The number of characters the font answers to.
  [[nodiscard]] std::size_t characterCount() const { return glyphs_.size(); }

  [[nodiscard]] int cellWidth() const { return cellWidth_; }
  [[nodiscard]] int cellHeight() const { return cellHeight_; }
  [[nodiscard]] int baseline() const { return baseline_; }

private:
  // Adds the composed glyphs whose base glyphs the font has, in new cells under the atlas's.
  void addComposedGlyphs();

  assets::Image atlas_;
  int cellWidth_ = 0;
  int cellHeight_ = 0;
  int baseline_ = 0;
  std::map<char32_t, BitmapGlyph> glyphs_;
};

} // namespace dwe::ui
