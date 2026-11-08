#include <dwe/assets/image.hpp>
#include <dwe/ui/bitmap_font.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::ui {
namespace {

// A test font in the original's layout (12 x 11 cells, baseline 10) with a few glyphs of our own:
// a small letter as tall as the font's x-height, a capital as tall as its capitals, a dotted i, !
// and ?.
constexpr int CellWidth = 12;
constexpr int CellHeight = 11;
constexpr int Baseline = 10;

struct TestGlyph {
  std::string_view character;
  int advance;
  std::array<std::string_view, CellHeight> rows;
};

constexpr std::array<TestGlyph, 8> Glyphs{{
    {"a", 8, {"", "", "", ".####", "#....#", "#....#", "######", "#....#", ".####", "", ""}},
    {"A",
     12,
     {"...###", "..#...#", ".#.....#", "#.......#", "#.......#", "#########", "#.......#",
      "#.......#", "#.......#", "#.......#", ""}},
    {"i", 4, {"#", "", "", "#", "#", "#", "#", "#", "#", "", ""}},
    {"!", 5, {"..#", "..#", "..#", "..#", "..#", "..#", "..#", "", "", "..#", ""}},
    {"?", 8, {".####", "#....#", ".....#", "....#", "...#", "...#", "", "", "", "...#", ""}},
    {"é", 8, {"", "", "#####", "", "", "", "", "", "", "", ""}},
    {"e", 8, {"", "", "", ".####", "#....#", "######", "#", "#....#", ".####", "", ""}},
    {"E", 12, {"#########", "#", "#", "#", "########", "#", "#", "#", "#", "#########", ""}},
}};

struct TestFont {
  std::string meta;
  assets::Image atlas;
};

TestFont makeFont(std::size_t count) {
  TestFont font;
  font.atlas.width = static_cast<std::uint32_t>(CellWidth * count);
  font.atlas.height = CellHeight;
  font.atlas.pixels.resize(std::size_t{font.atlas.width} * font.atlas.height * 4);
  font.meta = std::format("cell_width: {}\ncell_height: {}\nglyphs:\n", CellWidth, CellHeight);
  for (std::size_t index = 0; index < count; ++index) {
    const TestGlyph& glyph = Glyphs[index];
    font.meta += std::format("- character: \"{}\"\n  x: {}\n  y: 0\n  width: {}\n", glyph.character,
                             index * CellWidth, glyph.advance);
    for (std::size_t y = 0; y < glyph.rows.size(); ++y) {
      for (std::size_t x = 0; x < glyph.rows[y].size(); ++x) {
        if (glyph.rows[y][x] == '#') {
          const std::size_t at = ((y * font.atlas.width) + (index * CellWidth) + x) * 4;
          for (std::size_t channel = 0; channel < 4; ++channel) {
            font.atlas.pixels[at + channel] = std::byte{0xFF};
          }
        }
      }
    }
  }
  return font;
}

BitmapFont parsed(std::size_t count) {
  TestFont font = makeFont(count);
  auto parsedFont = BitmapFont::parse(font.meta, std::move(font.atlas), Baseline);
  REQUIRE_MESSAGE(parsedFont.has_value(), parsedFont.error().message);
  return std::move(*parsedFont);
}

// A glyph's cell as rows of '#' and '.', trailing dots cut.
std::vector<std::string> cellOf(const BitmapFont& font, char32_t character) {
  const BitmapGlyph* glyph = font.glyph(character);
  REQUIRE(glyph != nullptr);
  const assets::Image ink = font.inkAtlas(1);
  std::vector<std::string> rows;
  for (int y = 0; y < CellHeight; ++y) {
    std::string row;
    for (int x = 0; x < CellWidth; ++x) {
      const std::size_t at = ((static_cast<std::size_t>(glyph->y + y) * ink.width) +
                              static_cast<std::size_t>(glyph->x + x)) *
                             4;
      row += ink.pixels[at + 3] != std::byte{0} ? '#' : '.';
    }
    row.erase(row.find_last_not_of('.') + 1);
    rows.push_back(row);
  }
  return rows;
}

std::vector<std::string> rowsOf(const std::array<std::string_view, CellHeight>& rows) {
  return {rows.begin(), rows.end()};
}

} // namespace

TEST_CASE("a small accented letter is its base letter with the mark over it, one row apart") {
  const BitmapFont font = parsed(Glyphs.size());
  std::vector<std::string> expected = rowsOf(Glyphs[0].rows);
  expected[0] = "...#";
  expected[1] = "..#";
  CHECK(cellOf(font, U'á') == expected);
  CHECK(font.glyph(U'á')->advance == 8);
  // The other marks on the same letter: grave, circumflex, diaeresis, tilde.
  CHECK(cellOf(font, U'à')[0] == "..#");
  CHECK(cellOf(font, U'à')[1] == "...#");
  CHECK(cellOf(font, U'â')[0] == "...#");
  CHECK(cellOf(font, U'â')[1] == "..#.#");
  CHECK(cellOf(font, U'ä')[0].empty());
  CHECK(cellOf(font, U'ä')[1] == "..#.#");
  CHECK(cellOf(font, U'ã')[0] == "..#.#");
  CHECK(cellOf(font, U'ã')[1] == ".#.#");
}

TEST_CASE("an accented i loses its dot for the mark") {
  const BitmapFont font = parsed(Glyphs.size());
  CHECK(cellOf(font, U'í') ==
        std::vector<std::string>{".#", "#", "", "#", "#", "#", "#", "#", "#", "", ""});
  CHECK(font.glyph(U'í')->advance == 4);
}

TEST_CASE("an accented capital is squeezed under its mark to stay inside the line") {
  const BitmapFont font = parsed(Glyphs.size());
  const std::vector<std::string> acute = cellOf(font, U'Á');
  // The mark on the two top rows, an empty row, and the capital from row 3 down to its own feet
  // (row 9): its long legs give the rows.
  CHECK(acute[0] == ".....#");
  CHECK(acute[1] == "....#");
  CHECK(acute[2].empty());
  CHECK(acute[3] == "...###");
  CHECK(acute[9] == "#.......#");
  CHECK(acute[10].empty());
  CHECK(font.glyph(U'Á')->advance == 12);
  // The one-row diaeresis takes one row less.
  const std::vector<std::string> diaeresis = cellOf(font, U'Ä');
  CHECK(diaeresis[0] == "...#.#");
  CHECK(diaeresis[1].empty());
  CHECK(diaeresis[2] == "...###");
  CHECK(diaeresis[9] == "#.......#");
}

TEST_CASE("the inverted signs are the font's own turned over") {
  const BitmapFont font = parsed(Glyphs.size());
  CHECK(cellOf(font, U'¡') == std::vector<std::string>{"..#", "", "", "..#", "..#", "..#", "..#",
                                                       "..#", "..#", "..#", ""});
  CHECK(font.glyph(U'¡')->advance == 5);
  const std::vector<std::string> question = cellOf(font, U'¿');
  CHECK(question[0] == "..#");
  CHECK(question[4] == "..#");
  CHECK(question[9] == ".####");
  CHECK(font.glyph(U'¿')->advance == 8);
}

TEST_CASE("signs without a base are drawn whole, and a letter the font has is kept") {
  const BitmapFont font = parsed(Glyphs.size());
  for (char32_t sign : {U'«', U'»', U'º', U'ª', U'…', U'·'}) {
    CAPTURE(static_cast<std::uint32_t>(sign));
    CHECK(font.glyph(sign) != nullptr);
  }
  CHECK(font.glyph(U'«')->advance == 8);
  // The font's own é stays as it is.
  CHECK(cellOf(font, U'é') == rowsOf(Glyphs[5].rows));
  // A letter whose base the font lacks is not made up: no o, so no ó.
  CHECK(font.glyph(U'ó') == nullptr);
  CHECK(font.glyph(U'Ó') == nullptr);
}

TEST_CASE("the other languages' signs are drawn whole and the small ligatures share a stroke") {
  const BitmapFont font = parsed(Glyphs.size());
  for (char32_t sign : {U'ß', U'„', U'‚', U'‹', U'›'}) {
    CAPTURE(static_cast<std::uint32_t>(sign));
    CHECK(font.glyph(sign) != nullptr);
  }
  CHECK(font.glyph(U'ß')->advance == 8);
  // æ: the a, then the e from the a's last column, in one cell.
  CHECK(cellOf(font, U'æ') == std::vector<std::string>{"", "", "", ".####.####", "#....#....#",
                                                       "#....######", "######", "#....#....#",
                                                       ".####.####", "", ""});
  CHECK(font.glyph(U'æ')->advance == 12);
  // Æ would not fit in the cell (nor œ without an o): the fallback font draws them.
  CHECK(font.glyph(U'Æ') == nullptr);
  CHECK(font.glyph(U'œ') == nullptr);
}

TEST_CASE("a space advances as far as the glyph before it only in the dialogue family") {
  const BitmapGlyph space{.x = 0, .y = 0, .advance = 4};
  const BitmapGlyph letter{.x = 0, .y = 0, .advance = 8};
  int previous = 0;
  // With nothing before it, a space takes its own advance.
  CHECK(glyphAdvance(space, U' ', true, previous) == 4);
  CHECK(glyphAdvance(letter, U'a', true, previous) == 8);
  CHECK(glyphAdvance(space, U' ', true, previous) == 8);
  // A second space keeps the letter's advance; so does the full-width one.
  CHECK(glyphAdvance(space, U'　', true, previous) == 8);
  CHECK(glyphAdvance(space, U' ', false, previous) == 4);
  CHECK(previous == 8);
}

TEST_CASE("glyphs land on the original's grid of 4 pixels, rounding down") {
  CHECK(onGlyphGrid(0.0F, 1) == 0.0F);
  CHECK(onGlyphGrid(5.0F, 1) == 4.0F);
  CHECK(onGlyphGrid(8.0F, 1) == 8.0F);
  CHECK(onGlyphGrid(11.0F, 1) == 8.0F);
  // Three times larger, the grid is 12 pixels.
  CHECK(onGlyphGrid(15.0F, 3) == 12.0F);
  CHECK(onGlyphGrid(24.0F, 3) == 24.0F);
}

} // namespace dwe::ui
