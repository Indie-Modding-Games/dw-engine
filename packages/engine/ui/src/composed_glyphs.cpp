#include <dwe/ui/bitmap_font.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// The letters and signs the original's bitmap font lacks, composed in its own pixel style: an
// accented letter is the font's base letter with a mark drawn over (or under) it, ¡ and ¿ are its
// ! and ? turned over, a few signs («, », º, ª, …, ·, ß, the low and single quotes) are small
// drawings of our own, and the small ligatures (æ, œ) join two of its letters on a shared stroke.
// Nothing here comes from the game's data: the marks and drawings are written below, and the base
// glyphs are read from the atlas the player's copy gives.
namespace dwe::ui {
namespace {

constexpr std::size_t RgbaBytes = 4;
constexpr std::size_t AlphaByte = 3;
constexpr std::byte Opaque{0xFF};
constexpr char Ink = '#';

// One glyph's ink, cell-sized: row by row, true where there is ink.
class Cell {
public:
  Cell(int width, int height)
      : width_{width}, height_{height},
        ink_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), false) {}

  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] bool at(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_ && ink_[index(x, y)];
  }
  void set(int x, int y, bool ink = true) {
    if (x >= 0 && y >= 0 && x < width_ && y < height_) {
      ink_[index(x, y)] = ink;
    }
  }
  [[nodiscard]] bool rowHasInk(int y) const {
    return std::ranges::any_of(std::views::iota(0, width_), [&](int x) { return at(x, y); });
  }
  [[nodiscard]] bool columnHasInk(int x) const {
    return std::ranges::any_of(std::views::iota(0, height_), [&](int y) { return at(x, y); });
  }
  [[nodiscard]] std::vector<bool> row(int y) const {
    const auto begin = ink_.begin() + static_cast<std::ptrdiff_t>(index(0, y));
    return {begin, begin + width_};
  }
  void setRow(int y, const std::vector<bool>& ink) {
    for (int x = 0; x < width_; ++x) {
      set(x, y, ink[static_cast<std::size_t>(x)]);
    }
  }

private:
  [[nodiscard]] std::size_t index(int x, int y) const {
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_)) +
           static_cast<std::size_t>(x);
  }

  int width_;
  int height_;
  std::vector<bool> ink_;
};

// The rows and columns a cell has ink in, or nothing for an empty cell.
struct InkBox {
  int top = 0;
  int bottom = 0;
  int left = 0;
  int right = 0;
};

std::optional<InkBox> inkBox(const Cell& cell) {
  InkBox box{.top = cell.height(), .bottom = -1, .left = cell.width(), .right = -1};
  for (int y = 0; y < cell.height(); ++y) {
    for (int x = 0; x < cell.width(); ++x) {
      if (cell.at(x, y)) {
        box.top = std::min(box.top, y);
        box.bottom = std::max(box.bottom, y);
        box.left = std::min(box.left, x);
        box.right = std::max(box.right, x);
      }
    }
  }
  if (box.bottom < 0) {
    return std::nullopt;
  }
  return box;
}

// A small drawing in rows of '#' (ink) and '.'.
using Drawing = std::span<const std::string_view>;

void draw(Cell& cell, Drawing drawing, int left, int top) {
  for (std::size_t y = 0; y < drawing.size(); ++y) {
    for (std::size_t x = 0; x < drawing[y].size(); ++x) {
      if (drawing[y][x] == Ink) {
        cell.set(left + static_cast<int>(x), top + static_cast<int>(y));
      }
    }
  }
}

// The marks, drawn one pixel thick like the font's strokes.
constexpr std::array<std::string_view, 2> Acute{".#", "#."};
constexpr std::array<std::string_view, 2> Grave{"#.", ".#"};
constexpr std::array<std::string_view, 2> Circumflex{".#.", "#.#"};
constexpr std::array<std::string_view, 1> Diaeresis{"#.#"};
constexpr std::array<std::string_view, 2> Tilde{".#.#", "#.#."};
constexpr std::array<std::string_view, 2> Cedilla{".#", "#."};

enum class MarkPlace : std::uint8_t { Above, Below };

struct Mark {
  Drawing drawing;
  MarkPlace place = MarkPlace::Above;
};

constexpr Mark AcuteMark{.drawing = Acute};
constexpr Mark GraveMark{.drawing = Grave};
constexpr Mark CircumflexMark{.drawing = Circumflex};
constexpr Mark DiaeresisMark{.drawing = Diaeresis};
constexpr Mark TildeMark{.drawing = Tilde};
constexpr Mark CedillaMark{.drawing = Cedilla, .place = MarkPlace::Below};

// A letter and the base letter and mark it is composed of.
struct Accented {
  char32_t character;
  char32_t base;
  const Mark* mark;
};

constexpr std::array AccentedLetters{
    Accented{.character = U'á', .base = U'a', .mark = &AcuteMark},
    Accented{.character = U'à', .base = U'a', .mark = &GraveMark},
    Accented{.character = U'â', .base = U'a', .mark = &CircumflexMark},
    Accented{.character = U'ä', .base = U'a', .mark = &DiaeresisMark},
    Accented{.character = U'ã', .base = U'a', .mark = &TildeMark},
    Accented{.character = U'é', .base = U'e', .mark = &AcuteMark},
    Accented{.character = U'è', .base = U'e', .mark = &GraveMark},
    Accented{.character = U'ê', .base = U'e', .mark = &CircumflexMark},
    Accented{.character = U'ë', .base = U'e', .mark = &DiaeresisMark},
    Accented{.character = U'í', .base = U'i', .mark = &AcuteMark},
    Accented{.character = U'ì', .base = U'i', .mark = &GraveMark},
    Accented{.character = U'î', .base = U'i', .mark = &CircumflexMark},
    Accented{.character = U'ï', .base = U'i', .mark = &DiaeresisMark},
    Accented{.character = U'ó', .base = U'o', .mark = &AcuteMark},
    Accented{.character = U'ò', .base = U'o', .mark = &GraveMark},
    Accented{.character = U'ô', .base = U'o', .mark = &CircumflexMark},
    Accented{.character = U'ö', .base = U'o', .mark = &DiaeresisMark},
    Accented{.character = U'õ', .base = U'o', .mark = &TildeMark},
    Accented{.character = U'ú', .base = U'u', .mark = &AcuteMark},
    Accented{.character = U'ù', .base = U'u', .mark = &GraveMark},
    Accented{.character = U'û', .base = U'u', .mark = &CircumflexMark},
    Accented{.character = U'ü', .base = U'u', .mark = &DiaeresisMark},
    Accented{.character = U'ñ', .base = U'n', .mark = &TildeMark},
    Accented{.character = U'ç', .base = U'c', .mark = &CedillaMark},
    Accented{.character = U'ý', .base = U'y', .mark = &AcuteMark},
    Accented{.character = U'ÿ', .base = U'y', .mark = &DiaeresisMark},
    Accented{.character = U'Á', .base = U'A', .mark = &AcuteMark},
    Accented{.character = U'À', .base = U'A', .mark = &GraveMark},
    Accented{.character = U'Â', .base = U'A', .mark = &CircumflexMark},
    Accented{.character = U'Ä', .base = U'A', .mark = &DiaeresisMark},
    Accented{.character = U'Ã', .base = U'A', .mark = &TildeMark},
    Accented{.character = U'É', .base = U'E', .mark = &AcuteMark},
    Accented{.character = U'È', .base = U'E', .mark = &GraveMark},
    Accented{.character = U'Ê', .base = U'E', .mark = &CircumflexMark},
    Accented{.character = U'Ë', .base = U'E', .mark = &DiaeresisMark},
    Accented{.character = U'Í', .base = U'I', .mark = &AcuteMark},
    Accented{.character = U'Ì', .base = U'I', .mark = &GraveMark},
    Accented{.character = U'Î', .base = U'I', .mark = &CircumflexMark},
    Accented{.character = U'Ï', .base = U'I', .mark = &DiaeresisMark},
    Accented{.character = U'Ó', .base = U'O', .mark = &AcuteMark},
    Accented{.character = U'Ò', .base = U'O', .mark = &GraveMark},
    Accented{.character = U'Ô', .base = U'O', .mark = &CircumflexMark},
    Accented{.character = U'Ö', .base = U'O', .mark = &DiaeresisMark},
    Accented{.character = U'Õ', .base = U'O', .mark = &TildeMark},
    Accented{.character = U'Ú', .base = U'U', .mark = &AcuteMark},
    Accented{.character = U'Ù', .base = U'U', .mark = &GraveMark},
    Accented{.character = U'Û', .base = U'U', .mark = &CircumflexMark},
    Accented{.character = U'Ü', .base = U'U', .mark = &DiaeresisMark},
    Accented{.character = U'Ñ', .base = U'N', .mark = &TildeMark},
    Accented{.character = U'Ç', .base = U'C', .mark = &CedillaMark},
    Accented{.character = U'Ý', .base = U'Y', .mark = &AcuteMark},
};

// A sign turned over from one of the font's: upside down (¡ from !), or also mirrored (¿ from ?).
struct Turned {
  char32_t character;
  char32_t base;
  bool mirrored;
};

constexpr std::array TurnedSigns{
    Turned{.character = U'¡', .base = U'!', .mirrored = false},
    Turned{.character = U'¿', .base = U'?', .mirrored = true},
};

// The signs drawn whole: rows of the drawing from `top`, and the advance.
struct Drawn {
  char32_t character;
  Drawing drawing;
  int top;
  int advance;
};

constexpr std::array<std::string_view, 5> LeftGuillemet{"..#..#", ".#..#.", "#..#..", ".#..#.",
                                                        "..#..#"};
constexpr std::array<std::string_view, 5> RightGuillemet{"#..#..", ".#..#.", "..#..#", ".#..#.",
                                                         "#..#.."};
constexpr std::array<std::string_view, 6> MasculineOrdinal{".##.", "#..#", "#..#",
                                                           ".##.", "....", "####"};
constexpr std::array<std::string_view, 6> FeminineOrdinal{".##.", "#..#", "#.##",
                                                          ".#.#", "....", "####"};
constexpr std::array<std::string_view, 2> Ellipsis{".##..##..##", ".##..##..##"};
constexpr std::array<std::string_view, 2> MiddleDot{"..##", "..##"};
// The German sharp s, as tall as the font's b and h (rows 0 to 8) and as wide as their bowls.
constexpr std::array<std::string_view, 9> SharpS{".####.", "#....#", "#....#", "#...#.", "#.##..",
                                                 "#...#.", "#....#", "#....#", "#.###."};
// The low quotes (German and others) sit where the comma does; the single guillemets are the
// double ones' halves.
constexpr std::array<std::string_view, 3> LowDoubleQuote{"..#.#", "..#.#", ".#.#."};
constexpr std::array<std::string_view, 3> LowSingleQuote{"..#", "..#", ".#."};
constexpr std::array<std::string_view, 5> LeftSingleGuillemet{"..#", ".#.", "#..", ".#.", "..#"};
constexpr std::array<std::string_view, 5> RightSingleGuillemet{"#..", ".#.", "..#", ".#.", "#.."};

// In rows of the original's 11-row cells: the guillemets sit on the small letters' middle, the
// ordinals at the capitals' top, the dots where the font's full stop has its own.
constexpr int GuillemetTop = 3;
constexpr int OrdinalTop = 0;
constexpr int StopTop = 7;
constexpr int MiddleDotTop = 4;
constexpr int TallTop = 0;
constexpr int LowQuoteTop = 7;
constexpr int LetterAdvance = 8;
constexpr int SingleGuillemetAdvance = 5;
constexpr int SmallAdvance = 8;
constexpr int OrdinalAdvance = 6;
constexpr int EllipsisAdvance = 12;
constexpr int DotAdvance = 5;

constexpr std::array DrawnSigns{
    Drawn{
        .character = U'«', .drawing = LeftGuillemet, .top = GuillemetTop, .advance = SmallAdvance},
    Drawn{
        .character = U'»', .drawing = RightGuillemet, .top = GuillemetTop, .advance = SmallAdvance},
    Drawn{.character = U'º',
          .drawing = MasculineOrdinal,
          .top = OrdinalTop,
          .advance = OrdinalAdvance},
    Drawn{.character = U'ª',
          .drawing = FeminineOrdinal,
          .top = OrdinalTop,
          .advance = OrdinalAdvance},
    Drawn{.character = U'…', .drawing = Ellipsis, .top = StopTop, .advance = EllipsisAdvance},
    Drawn{.character = U'·', .drawing = MiddleDot, .top = MiddleDotTop, .advance = DotAdvance},
    Drawn{.character = U'ß', .drawing = SharpS, .top = TallTop, .advance = LetterAdvance},
    Drawn{
        .character = U'„', .drawing = LowDoubleQuote, .top = LowQuoteTop, .advance = LetterAdvance},
    Drawn{.character = U'‚', .drawing = LowSingleQuote, .top = LowQuoteTop, .advance = DotAdvance},
    Drawn{.character = U'‹',
          .drawing = LeftSingleGuillemet,
          .top = GuillemetTop,
          .advance = SingleGuillemetAdvance},
    Drawn{.character = U'›',
          .drawing = RightSingleGuillemet,
          .top = GuillemetTop,
          .advance = SingleGuillemetAdvance},
};

// The ligatures made of two of the font's letters, the second drawn from the first's last inked
// column on, so the two share that stroke. One wider than the cell is left to the fallback font
// (the capitals Æ and Œ in the original's cells).
struct Joined {
  char32_t character;
  char32_t first;
  char32_t second;
};

constexpr std::array JoinedLetters{
    Joined{.character = U'æ', .first = U'a', .second = U'e'},
    Joined{.character = U'œ', .first = U'o', .second = U'e'},
    Joined{.character = U'Æ', .first = U'A', .second = U'E'},
    Joined{.character = U'Œ', .first = U'O', .second = U'E'},
};

// Characters drawn with another one's glyph: the curly quotes the original only has closing, and
// the dashes and minus as its hyphen.
struct Alias {
  char32_t character;
  char32_t glyph;
};

constexpr std::array Aliases{
    Alias{.character = U'“', .glyph = U'”'}, Alias{.character = U'‘', .glyph = U'’'},
    Alias{.character = U'−', .glyph = U'-'}, Alias{.character = U'–', .glyph = U'-'},
    Alias{.character = U'—', .glyph = U'-'},
};

// A dot over the body (i, j) that a mark replaces: ink rows at the top no taller than this, with an
// empty row between them and the rest.
constexpr int MaxDotRows = 2;

void removeDot(Cell& cell, const InkBox& box) {
  int gap = box.top;
  while (gap <= box.bottom && cell.rowHasInk(gap)) {
    ++gap;
  }
  if (gap > box.bottom || gap - box.top > MaxDotRows) {
    return;
  }
  for (int y = box.top; y < gap; ++y) {
    cell.setRow(y, std::vector<bool>(static_cast<std::size_t>(cell.width()), false));
  }
}

// How much merging two neighbouring rows costs: nothing for a row repeated three times or more
// (a long stroke), a little for a repeated pair (a short one), else the pixels that differ. Merging
// keeps the row with more ink.
constexpr double PairCost = 2.5;
// Of two equal costs, the merge nearer the letter's middle wins.
constexpr double MiddleWeight = 0.01;

int runLength(const std::vector<std::vector<bool>>& rows, std::size_t at) {
  std::size_t first = at;
  while (first > 0 && rows[first - 1] == rows[at]) {
    --first;
  }
  std::size_t last = at;
  while (last + 1 < rows.size() && rows[last + 1] == rows[at]) {
    ++last;
  }
  return static_cast<int>(last - first + 1);
}

double mergeCost(const std::vector<std::vector<bool>>& rows, std::size_t at) {
  const std::vector<bool>& upper = rows[at];
  const std::vector<bool>& lower = rows[at + 1];
  double cost = 0.0;
  if (upper == lower) {
    constexpr int LongStroke = 3;
    cost = runLength(rows, at) >= LongStroke ? 0.0 : PairCost;
  } else {
    for (std::size_t x = 0; x < upper.size(); ++x) {
      cost += upper[x] != lower[x] ? 1.0 : 0.0;
    }
  }
  const double middle = static_cast<double>(rows.size() - 1) / 2.0;
  return cost + (std::abs((static_cast<double>(at) + 0.5) - middle) * MiddleWeight);
}

// Squeezes the ink rows from `top` to `bottom` into `newTop` to `newBottom` by merging the
// neighbouring rows that cost least, so a capital makes room for its mark inside the line.
void squeeze(Cell& cell, int top, int bottom, int newTop, int newBottom) {
  std::vector<std::vector<bool>> rows;
  for (int y = top; y <= bottom; ++y) {
    rows.push_back(cell.row(y));
  }
  const auto target = static_cast<std::size_t>(std::max(1, newBottom - newTop + 1));
  while (rows.size() > target) {
    std::size_t best = 0;
    for (std::size_t at = 1; at + 1 < rows.size(); ++at) {
      if (mergeCost(rows, at) < mergeCost(rows, best)) {
        best = at;
      }
    }
    const auto inkOf = [](const std::vector<bool>& row) { return std::ranges::count(row, true); };
    if (inkOf(rows[best + 1]) > inkOf(rows[best])) {
      rows[best] = rows[best + 1];
    }
    rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(best) + 1);
  }
  const std::vector<bool> empty(static_cast<std::size_t>(cell.width()), false);
  for (int y = top; y <= bottom; ++y) {
    cell.setRow(y, empty);
  }
  for (std::size_t at = 0; at < rows.size(); ++at) {
    cell.setRow(newTop + static_cast<int>(at), rows[at]);
  }
}

// The base letter with the mark centred over its ink (or under it), one empty row between them. A
// letter too tall for that (the capitals) is squeezed down to its feet's row to make room.
Cell accented(Cell cell, const Mark& mark, int advance) {
  if (mark.place == MarkPlace::Above) {
    if (const auto dotted = inkBox(cell)) {
      removeDot(cell, *dotted);
    }
  }
  const auto found = inkBox(cell);
  if (!found) {
    return cell;
  }
  const InkBox box = *found;
  const auto height = static_cast<int>(mark.drawing.size());
  const auto width = static_cast<int>(mark.drawing.front().size());
  const double centre = static_cast<double>(box.left + box.right) / 2.0;
  int left = static_cast<int>(std::floor(centre - (static_cast<double>(width - 1) / 2.0) + 0.5));
  left = advance >= width ? std::clamp(left, 0, advance - width) : 0;
  if (mark.place == MarkPlace::Below) {
    const int lastRow = cell.height() - 1 - height;
    const int bottom = std::min(box.bottom, lastRow);
    if (box.bottom > lastRow) {
      squeeze(cell, box.top, box.bottom, box.top, lastRow);
    }
    draw(cell, mark.drawing, left, bottom + 1);
    return cell;
  }
  if (box.top < height + 1) {
    squeeze(cell, box.top, box.bottom, height + 1, box.bottom);
    draw(cell, mark.drawing, left, 0);
    return cell;
  }
  draw(cell, mark.drawing, left, box.top - 1 - height);
  return cell;
}

// Two letters on one shared column, or nothing when they do not fit in the cell. The advance keeps
// the gap the first letter leaves after its ink.
std::optional<std::pair<Cell, int>> joined(const Cell& first, int firstAdvance,
                                           const Cell& second) {
  const auto left = inkBox(first);
  const auto right = inkBox(second);
  if (!left || !right) {
    return std::nullopt;
  }
  const int shift = left->right - right->left;
  const int inkRight = right->right + shift;
  if (inkRight >= first.width()) {
    return std::nullopt;
  }
  Cell result = first;
  for (int y = 0; y < second.height(); ++y) {
    for (int x = right->left; x <= right->right; ++x) {
      if (second.at(x, y)) {
        result.set(x + shift, y);
      }
    }
  }
  const int gap = std::max(1, firstAdvance - 1 - left->right);
  return std::pair{std::move(result), std::min(first.width(), inkRight + 1 + gap)};
}

Cell turned(const Cell& cell, bool mirrored) {
  Cell result{cell.width(), cell.height()};
  const auto box = inkBox(cell);
  if (!box) {
    return result;
  }
  for (int y = box->top; y <= box->bottom; ++y) {
    for (int x = box->left; x <= box->right; ++x) {
      const int toX = mirrored ? box->left + box->right - x : x;
      result.set(toX, box->top + box->bottom - y, cell.at(x, y));
    }
  }
  return result;
}

} // namespace

void BitmapFont::addComposedGlyphs() {
  const auto readCell = [this](const BitmapGlyph& glyph) {
    Cell cell{cellWidth_, cellHeight_};
    for (int y = 0; y < cellHeight_; ++y) {
      for (int x = 0; x < cellWidth_; ++x) {
        const std::size_t at = ((static_cast<std::size_t>(glyph.y + y) * atlas_.width) +
                                static_cast<std::size_t>(glyph.x + x)) *
                               RgbaBytes;
        cell.set(x, y, atlas_.pixels[at + AlphaByte] != std::byte{0});
      }
    }
    return cell;
  };

  std::vector<std::pair<char32_t, std::pair<Cell, int>>> added;
  const auto missing = [&](char32_t character) {
    return glyph(character) == nullptr &&
           std::ranges::none_of(added, [&](const auto& entry) { return entry.first == character; });
  };
  for (const Accented& letter : AccentedLetters) {
    const BitmapGlyph* base = glyph(letter.base);
    if (base != nullptr && missing(letter.character)) {
      added.push_back({letter.character,
                       {accented(readCell(*base), *letter.mark, base->advance), base->advance}});
    }
  }
  for (const Turned& sign : TurnedSigns) {
    const BitmapGlyph* base = glyph(sign.base);
    if (base != nullptr && missing(sign.character)) {
      added.push_back({sign.character, {turned(readCell(*base), sign.mirrored), base->advance}});
    }
  }
  for (const Joined& letter : JoinedLetters) {
    const BitmapGlyph* first = glyph(letter.first);
    const BitmapGlyph* second = glyph(letter.second);
    if (first != nullptr && second != nullptr && missing(letter.character)) {
      if (auto both = joined(readCell(*first), first->advance, readCell(*second))) {
        added.push_back({letter.character, std::move(*both)});
      }
    }
  }
  for (const Drawn& sign : DrawnSigns) {
    if (missing(sign.character)) {
      Cell cell{cellWidth_, cellHeight_};
      draw(cell, sign.drawing, 0, sign.top);
      added.push_back({sign.character, {std::move(cell), sign.advance}});
    }
  }

  // The new cells go in rows under the atlas's, as many to a row as the atlas is wide.
  if (!added.empty()) {
    const int columns = std::max(1, static_cast<int>(atlas_.width) / cellWidth_);
    const int firstRow = static_cast<int>(atlas_.height);
    const int rows = (static_cast<int>(added.size()) + columns - 1) / columns;
    const std::uint32_t width =
        std::max(atlas_.width, static_cast<std::uint32_t>(columns * cellWidth_));
    assets::Image grown{.width = width,
                        .height = atlas_.height + static_cast<std::uint32_t>(rows * cellHeight_),
                        .pixels = {}};
    grown.pixels.resize(std::size_t{grown.width} * grown.height * RgbaBytes);
    for (std::uint32_t y = 0; y < atlas_.height; ++y) {
      std::ranges::copy_n(atlas_.pixels.begin() + static_cast<std::ptrdiff_t>(
                                                      std::size_t{y} * atlas_.width * RgbaBytes),
                          static_cast<std::ptrdiff_t>(std::size_t{atlas_.width} * RgbaBytes),
                          grown.pixels.begin() + static_cast<std::ptrdiff_t>(
                                                     std::size_t{y} * grown.width * RgbaBytes));
    }
    for (std::size_t index = 0; index < added.size(); ++index) {
      const auto& [character, drawn] = added[index];
      const auto& [cell, advance] = drawn;
      const BitmapGlyph glyph{
          .x = static_cast<int>(index % static_cast<std::size_t>(columns)) * cellWidth_,
          .y = firstRow +
               (static_cast<int>(index / static_cast<std::size_t>(columns)) * cellHeight_),
          .advance = advance};
      for (int y = 0; y < cellHeight_; ++y) {
        for (int x = 0; x < cellWidth_; ++x) {
          if (cell.at(x, y)) {
            const std::size_t at = ((static_cast<std::size_t>(glyph.y + y) * grown.width) +
                                    static_cast<std::size_t>(glyph.x + x)) *
                                   RgbaBytes;
            std::fill_n(grown.pixels.begin() + static_cast<std::ptrdiff_t>(at), RgbaBytes, Opaque);
          }
        }
      }
      glyphs_.try_emplace(character, glyph);
    }
    atlas_ = std::move(grown);
  }

  for (const Alias& alias : Aliases) {
    if (const BitmapGlyph* target = glyph(alias.glyph); target != nullptr) {
      glyphs_.try_emplace(alias.character, *target);
    }
  }
}

} // namespace dwe::ui
