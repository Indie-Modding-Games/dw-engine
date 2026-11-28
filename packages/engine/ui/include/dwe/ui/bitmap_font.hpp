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

// The most blank columns a glyph leaves after its ink, as the i and the l do. The original's widths
// leave 4 after the j, the 1 and the narrow capitals, which reads as a space inside a word.
inline constexpr int MaxBlankAfterInk = 3;

// Whether a character is a space: the ASCII one or the full-width one the original's text uses.
[[nodiscard]] bool isSpace(char32_t character);

// How far a glyph moves the pen, in atlas pixels. With `dialogue` (the script's textboxes) a space
// moves it as far as the glyph before it did (script_textbox.c: drawString2 clears a space's cell
// but keeps the previous glyph's advance), or by its own advance with none before it.
// `previousAdvance` is the last glyph's that was not a space (0 for none), and is updated.
[[nodiscard]] int glyphAdvance(const BitmapGlyph& glyph, char32_t character, bool dialogue,
                               int& previousAdvance);

// A bitmap font as dwx extracts the original's (docs/formats/texts.md): an atlas of equal cells,
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

  // The last column of a glyph's cell with ink, or -1 for a glyph without ink.
  [[nodiscard]] int inkRight(const BitmapGlyph& glyph) const;

  // The number of characters the font answers to.
  [[nodiscard]] std::size_t characterCount() const { return glyphs_.size(); }

  [[nodiscard]] int cellWidth() const { return cellWidth_; }
  [[nodiscard]] int cellHeight() const { return cellHeight_; }
  [[nodiscard]] int baseline() const { return baseline_; }

private:
  // Adds the composed glyphs whose base glyphs the font has, in new cells under the atlas's.
  void addComposedGlyphs();

  // Narrows the advance of each glyph that leaves more than MaxBlankAfterInk after its ink. Digits
  // keep theirs so that numbers line up in columns.
  void trimBlankAfterInk();

  assets::Image atlas_;
  int cellWidth_ = 0;
  int cellHeight_ = 0;
  int baseline_ = 0;
  std::map<char32_t, BitmapGlyph> glyphs_;
};

} // namespace dwe::ui
