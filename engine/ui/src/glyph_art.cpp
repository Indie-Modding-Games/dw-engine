#include <dwe/ui/glyph_art.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::ui {
namespace {

// --- Pixel drawings: rows of '#' (ink) and '.' (clear), and for the positions '+' (the button
// meant, in the light ink) and 'o' (the other three, in the dark one).

struct Bitmap {
  std::string_view name;
  std::array<std::string_view, 5> rows;
};

// The glyphs' own 3 x 5 capitals.
constexpr std::array SmallFont{
    Bitmap{"A", {".#.", "#.#", "###", "#.#", "#.#"}},
    Bitmap{"B", {"##.", "#.#", "##.", "#.#", "##."}},
    Bitmap{"C", {".##", "#..", "#..", "#..", ".##"}},
    Bitmap{"D", {"##.", "#.#", "#.#", "#.#", "##."}},
    Bitmap{"E", {"###", "#..", "##.", "#..", "###"}},
    Bitmap{"F", {"###", "#..", "##.", "#..", "#.."}},
    Bitmap{"G", {".##", "#..", "#.#", "#.#", ".##"}},
    Bitmap{"H", {"#.#", "#.#", "###", "#.#", "#.#"}},
    Bitmap{"I", {"###", ".#.", ".#.", ".#.", "###"}},
    Bitmap{"J", {"..#", "..#", "..#", "#.#", ".#."}},
    Bitmap{"K", {"#.#", "#.#", "##.", "#.#", "#.#"}},
    Bitmap{"L", {"#..", "#..", "#..", "#..", "###"}},
    Bitmap{"M", {"#.#", "###", "###", "#.#", "#.#"}},
    Bitmap{"N", {"##.", "#.#", "#.#", "#.#", "#.#"}},
    Bitmap{"O", {"###", "#.#", "#.#", "#.#", "###"}},
    Bitmap{"P", {"##.", "#.#", "##.", "#..", "#.."}},
    Bitmap{"Q", {".#.", "#.#", "#.#", "##.", ".##"}},
    Bitmap{"R", {"##.", "#.#", "##.", "#.#", "#.#"}},
    Bitmap{"S", {".##", "#..", ".#.", "..#", "##."}},
    Bitmap{"T", {"###", ".#.", ".#.", ".#.", ".#."}},
    Bitmap{"U", {"#.#", "#.#", "#.#", "#.#", "###"}},
    Bitmap{"V", {"#.#", "#.#", "#.#", "#.#", ".#."}},
    Bitmap{"W", {"#.#", "#.#", "###", "###", "#.#"}},
    Bitmap{"X", {"#.#", "#.#", ".#.", "#.#", "#.#"}},
    Bitmap{"Y", {"#.#", "#.#", ".#.", ".#.", ".#."}},
    Bitmap{"Z", {"###", "..#", ".#.", "#..", "###"}},
    Bitmap{"0", {".#.", "#.#", "#.#", "#.#", ".#."}},
    Bitmap{"1", {".#.", "##.", ".#.", ".#.", "###"}},
    Bitmap{"2", {"##.", "..#", ".#.", "#..", "###"}},
    Bitmap{"3", {"##.", "..#", ".#.", "..#", "##."}},
    Bitmap{"4", {"#.#", "#.#", "###", "..#", "..#"}},
    Bitmap{"5", {"###", "#..", "##.", "..#", "##."}},
    Bitmap{"6", {".##", "#..", "###", "#.#", "###"}},
    Bitmap{"7", {"###", "..#", ".#.", ".#.", ".#."}},
    Bitmap{"8", {"###", "#.#", "###", "#.#", "###"}},
    Bitmap{"9", {"###", "#.#", "###", "..#", "##."}},
    Bitmap{"+", {"...", ".#.", "###", ".#.", "..."}},
    Bitmap{"-", {"...", "...", "###", "...", "..."}},
    Bitmap{"=", {"...", "###", "...", "###", "..."}},
    Bitmap{",", {"...", "...", "...", ".#.", "#.."}},
    Bitmap{".", {"...", "...", "...", "...", ".#."}},
    Bitmap{"?", {"##.", "..#", ".#.", "...", ".#."}},
};

// Larger letters for a face button that shows one (its family's letters), and the named symbols.
constexpr std::array Symbols{
    Bitmap{"A", {"..#..", ".#.#.", "#...#", "#####", "#...#"}},
    Bitmap{"B", {"####.", "#...#", "####.", "#...#", "####."}},
    Bitmap{"X", {"#...#", ".#.#.", "..#..", ".#.#.", "#...#"}},
    Bitmap{"Y", {"#...#", ".#.#.", "..#..", "..#..", "..#.."}},
    Bitmap{"@cross", {"##...##", "..#.#..", "...#...", "..#.#..", "##...##"}},
    Bitmap{"@circle", {".#####.", "#.....#", "#.....#", "#.....#", ".#####."}},
    Bitmap{"@square", {"#####", "#...#", "#...#", "#...#", "#####"}},
    Bitmap{"@triangle", {"...#...", "..#.#..", ".#...#.", "#.....#", "#######"}},
    Bitmap{"@south", {"...o...", ".......", "o.....o", "..+++..", "..+++.."}},
    Bitmap{"@east", {"...o...", "....+++", "o...+++", ".......", "...o..."}},
    Bitmap{"@west", {"...o...", "+++....", "+++...o", ".......", "...o..."}},
    Bitmap{"@north", {"..+++..", "..+++..", "o.....o", ".......", "...o..."}},
    Bitmap{"@space", {"...........", "...........", "...........", "#.........#", "###########"}},
    Bitmap{"@enter", {"....#", "....#", ".#..#", "#####", ".#..."}},
    Bitmap{"@backspace", {"..#..", ".#...", "#####", ".#...", "..#.."}},
    Bitmap{"@slash", {"..#", "..#", ".#.", "#..", "#.."}},
    Bitmap{"@up", {"..#..", ".###.", "#.#.#", "..#..", "..#.."}},
    Bitmap{"@down", {"..#..", "..#..", "#.#.#", ".###.", "..#.."}},
    Bitmap{"@left", {"..#..", ".#...", "#####", ".#...", "..#.."}},
    Bitmap{"@right", {"..#..", "...#.", "#####", "...#.", "..#.."}},
    Bitmap{"@dpad_up", {".+++.", "o.+.o", "ooooo", "o.o.o", ".ooo."}},
    Bitmap{"@dpad_down", {".ooo.", "o.o.o", "ooooo", "o.+.o", ".+++."}},
    Bitmap{"@dpad_left", {".ooo.", "+.o.o", "++ooo", "+.o.o", ".ooo."}},
    Bitmap{"@dpad_right", {".ooo.", "o.o.+", "ooo++", "o.o.+", ".ooo."}},
};

const Bitmap* findBitmap(std::span<const Bitmap> set, std::string_view name) {
  const auto found = std::ranges::find(set, name, &Bitmap::name);
  return found == set.end() ? nullptr : &*found;
}

int bitmapWidth(const Bitmap& bitmap) {
  return static_cast<int>(bitmap.rows.front().size());
}

// The bitmaps a symbol is drawn with, left to right, one clear column apart for a text.
std::vector<const Bitmap*> symbolBitmaps(std::string_view symbol) {
  if (const Bitmap* named = findBitmap(Symbols, symbol); named != nullptr) {
    return {named};
  }
  std::vector<const Bitmap*> letters;
  for (const char c : symbol) {
    const Bitmap* letter = findBitmap(SmallFont, std::string_view{&c, 1});
    letters.push_back(letter != nullptr ? letter : findBitmap(SmallFont, "?"));
  }
  if (letters.empty()) {
    letters.push_back(findBitmap(SmallFont, "?"));
  }
  return letters;
}

int symbolWidth(std::string_view symbol) {
  int width = 0;
  for (const Bitmap* bitmap : symbolBitmaps(symbol)) {
    width += bitmapWidth(*bitmap) + (width > 0 ? 1 : 0);
  }
  return width;
}

// --- The classic drawings: the original's greys and the light blue of its confirm button's mark.

struct Rgba {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 0;
};

constexpr Rgba Outline{24, 24, 24, 255};
constexpr Rgba Base{49, 49, 49, 255};
constexpr Rgba BaseLight{65, 65, 65, 255};
constexpr Rgba Cap{106, 106, 106, 255};
constexpr Rgba CapLight{139, 139, 139, 255};
constexpr Rgba CapEdge{90, 90, 90, 255};
constexpr Rgba Ink{98, 180, 246, 255};
constexpr Rgba InkShadow{74, 98, 139, 255};
constexpr Rgba White{255, 255, 255, 255};

constexpr int ClassicHeight = 12;
constexpr int ButtonWidth = 16;
constexpr int CapMargin = 3; // clear columns of cap on each side of the symbol
constexpr int Frames = 3;

class Canvas {
public:
  Canvas(int width, int height)
      : width_{width}, height_{height},
        pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {}

  void set(int x, int y, Rgba color) {
    if (x >= 0 && y >= 0 && x < width_ && y < height_) {
      pixels_[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width_)) +
              static_cast<std::size_t>(x)] = color;
    }
  }

  [[nodiscard]] assets::Image image() const {
    assets::Image image{.width = static_cast<std::uint32_t>(width_),
                        .height = static_cast<std::uint32_t>(height_),
                        .pixels = {}};
    image.pixels.reserve(pixels_.size() * 4);
    for (const Rgba& pixel : pixels_) {
      for (const std::uint8_t channel : {pixel.r, pixel.g, pixel.b, pixel.a}) {
        image.pixels.push_back(static_cast<std::byte>(channel));
      }
    }
    return image;
  }

  [[nodiscard]] int width() const { return width_; }

private:
  int width_;
  int height_;
  std::vector<Rgba> pixels_;
};

// An ellipse through pixel centres, filled with `fill` and ringed with `edge`.
void ellipse(Canvas& canvas, double cx, double cy, double rx, double ry, Rgba fill, Rgba edge) {
  const auto inside = [&](int x, int y) {
    const double dx = (x + 0.5 - cx) / rx;
    const double dy = (y + 0.5 - cy) / ry;
    return (dx * dx) + (dy * dy) <= 1.0;
  };
  for (int y = 0; y < ClassicHeight; ++y) {
    for (int x = 0; x < canvas.width(); ++x) {
      if (!inside(x, y)) {
        continue;
      }
      const bool rim =
          !inside(x - 1, y) || !inside(x + 1, y) || !inside(x, y - 1) || !inside(x, y + 1);
      canvas.set(x, y, rim ? edge : fill);
    }
  }
}

// A rectangle with its corner pixels cut, filled with `fill` and ringed with `edge`.
void roundRect(Canvas& canvas, int left, int top, int right, int bottom, Rgba fill, Rgba edge) {
  for (int y = top; y <= bottom; ++y) {
    for (int x = left; x <= right; ++x) {
      const bool corner = (x == left || x == right) && (y == top || y == bottom);
      if (corner) {
        continue;
      }
      const bool rim = x == left || x == right || y == top || y == bottom;
      canvas.set(x, y, rim ? edge : fill);
    }
  }
}

// The symbol's bitmaps at (left, top), in the ink with its shadow a pixel below.
void stamp(Canvas& canvas, std::string_view symbol, int left, int top) {
  int x = left;
  for (const Bitmap* bitmap : symbolBitmaps(symbol)) {
    for (int pass = 0; pass < 2; ++pass) {
      const int shadow = pass == 0 ? 1 : 0;
      for (int row = 0; row < static_cast<int>(bitmap->rows.size()); ++row) {
        const std::string_view line = bitmap->rows.at(static_cast<std::size_t>(row));
        for (int column = 0; column < static_cast<int>(line.size()); ++column) {
          const char cell = line[static_cast<std::size_t>(column)];
          if (cell == '.') {
            continue;
          }
          Rgba color = cell == 'o' ? InkShadow : Ink;
          if (pass == 0) {
            if (cell == 'o') {
              continue;
            }
            color = InkShadow;
          }
          canvas.set(x + column, top + row + shadow, color);
        }
      }
    }
    x += bitmapWidth(*bitmap) + 1;
  }
}

assets::Image classicButton(std::string_view symbol, int frame) {
  Canvas canvas{ButtonWidth, ClassicHeight};
  // The base the button sits in, then its top, a pixel lower each frame.
  ellipse(canvas, 8.0, 7.5, 7.5, 4.5, Base, Outline);
  for (int x = 3; x < 13; ++x) {
    canvas.set(x, 10, BaseLight);
  }
  const double top = 4.5 + frame;
  ellipse(canvas, 8.0, top, 6.5, 3.5, Cap, CapEdge);
  for (int x = 4; x < 12; ++x) {
    canvas.set(x, static_cast<int>(top) - 2, CapLight);
  }
  const int width = symbolWidth(symbol);
  stamp(canvas, symbol, (ButtonWidth - width) / 2, static_cast<int>(top) - 2);
  return canvas.image();
}

assets::Image classicCap(std::string_view symbol, int frame) {
  const int width = classicCapWidth(symbol);
  Canvas canvas{width, ClassicHeight};
  roundRect(canvas, 0, 3, width - 1, ClassicHeight - 1, Base, Outline);
  for (int x = 2; x < width - 2; ++x) {
    canvas.set(x, ClassicHeight - 2, BaseLight);
  }
  const int top = frame;
  roundRect(canvas, 1, top, width - 2, top + 8, Cap, CapEdge);
  for (int x = 2; x < width - 2; ++x) {
    canvas.set(x, top + 1, CapLight);
  }
  stamp(canvas, symbol, (width - symbolWidth(symbol)) / 2, top + 2);
  return canvas.image();
}

// --- The modern drawings: smooth white symbols, 4 x 4 samples a pixel.

constexpr int ModernSize = 64;
constexpr int Samples = 4;

struct Point {
  double x;
  double y;
};

double segmentDistance(Point p, Point a, Point b) {
  const double vx = b.x - a.x;
  const double vy = b.y - a.y;
  const double t =
      std::clamp((((p.x - a.x) * vx) + ((p.y - a.y) * vy)) / ((vx * vx) + (vy * vy)), 0.0, 1.0);
  return std::hypot(p.x - (a.x + (t * vx)), p.y - (a.y + (t * vy)));
}

template <typename Inside>
assets::Image modern(Inside inside) {
  assets::Image image{.width = ModernSize, .height = ModernSize, .pixels = {}};
  image.pixels.reserve(static_cast<std::size_t>(ModernSize) * ModernSize * 4);
  for (int y = 0; y < ModernSize; ++y) {
    for (int x = 0; x < ModernSize; ++x) {
      int covered = 0;
      for (int sy = 0; sy < Samples; ++sy) {
        for (int sx = 0; sx < Samples; ++sx) {
          // The centre of the image is (0, 0) and its sides at +-32.
          const Point p{.x = x + ((sx + 0.5) / Samples) - (ModernSize / 2.0),
                        .y = y + ((sy + 0.5) / Samples) - (ModernSize / 2.0)};
          covered += inside(p) ? 1 : 0;
        }
      }
      const auto alpha = static_cast<std::uint8_t>((covered * 255) / (Samples * Samples));
      for (const std::uint8_t channel : {White.r, White.g, White.b, alpha}) {
        image.pixels.push_back(static_cast<std::byte>(channel));
      }
    }
  }
  return image;
}

constexpr double Stroke = 3.5; // half the width of a symbol's line
constexpr double Reach = 17.0; // how far a symbol reaches from the centre

core::Result<assets::Image> modernSymbol(std::string_view symbol) {
  if (symbol == "@cross") {
    return modern([](Point p) {
      const double r = Reach - 2.0;
      return segmentDistance(p, {-r, -r}, {r, r}) <= Stroke ||
             segmentDistance(p, {-r, r}, {r, -r}) <= Stroke;
    });
  }
  if (symbol == "@circle") {
    return modern([](Point p) { return std::abs(std::hypot(p.x, p.y) - Reach) <= Stroke; });
  }
  if (symbol == "@square") {
    return modern([](Point p) {
      const double r = Reach - 2.0;
      const double d = std::max(std::abs(p.x), std::abs(p.y));
      return std::abs(d - r) <= Stroke;
    });
  }
  if (symbol == "@triangle") {
    return modern([](Point p) {
      const Point top{0.0, -Reach};
      const Point left{-Reach, Reach * 0.75};
      const Point right{Reach, Reach * 0.75};
      return segmentDistance(p, top, left) <= Stroke || segmentDistance(p, left, right) <= Stroke ||
             segmentDistance(p, right, top) <= Stroke;
    });
  }
  // An arrow, as on the arrow keys: a shaft and a head (a value's arrows, the change hint).
  constexpr std::array Arrows{std::pair<std::string_view, Point>{"@left", {-1.0, 0.0}},
                              std::pair<std::string_view, Point>{"@right", {1.0, 0.0}},
                              std::pair<std::string_view, Point>{"@up", {0.0, -1.0}},
                              std::pair<std::string_view, Point>{"@down", {0.0, 1.0}}};
  const auto arrow = std::ranges::find(Arrows, symbol, &std::pair<std::string_view, Point>::first);
  if (arrow != Arrows.end()) {
    const Point to = arrow->second;
    return modern([to](Point p) {
      const double r = Reach - 2.0;
      const double head = r * 0.7;
      const Point tip{.x = to.x * r, .y = to.y * r};
      const Point tail{.x = -to.x * r, .y = -to.y * r};
      // The head's two strokes go back from the tip, one to each side of the shaft.
      const Point back{.x = tip.x - (to.x * head), .y = tip.y - (to.y * head)};
      const Point side{.x = -to.y * head, .y = to.x * head};
      return segmentDistance(p, tail, tip) <= Stroke ||
             segmentDistance(p, tip, {.x = back.x + side.x, .y = back.y + side.y}) <= Stroke ||
             segmentDistance(p, tip, {.x = back.x - side.x, .y = back.y - side.y}) <= Stroke;
    });
  }
  struct Position {
    std::string_view name;
    Point at;
  };
  constexpr std::array Positions{Position{"@south", {0.0, Reach}}, Position{"@east", {Reach, 0.0}},
                                 Position{"@west", {-Reach, 0.0}},
                                 Position{"@north", {0.0, -Reach}}};
  const auto meant = std::ranges::find(Positions, symbol, &Position::name);
  if (meant != Positions.end()) {
    return modern([&](Point p) {
      for (const Position& position : Positions) {
        const double d = std::hypot(p.x - position.at.x, p.y - position.at.y);
        // The button meant is a full dot, the other three are rings.
        if (position.name == meant->name ? d <= 9.0 : std::abs(d - 7.0) <= 1.5) {
          return true;
        }
      }
      return false;
    });
  }
  return core::fail(std::format("no modern glyph '{}'", symbol));
}

} // namespace

int classicCapWidth(std::string_view symbol) {
  return std::max(ButtonWidth, symbolWidth(symbol) + (2 * CapMargin));
}

core::Result<assets::Image> drawGlyph(std::string_view path) {
  // modern/<symbol>, or classic/<button|cap>/<symbol>/<frame>.
  constexpr std::string_view ModernPrefix = "modern/";
  constexpr std::string_view ClassicPrefix = "classic/";
  if (path.starts_with(ModernPrefix)) {
    return modernSymbol(path.substr(ModernPrefix.size()));
  }
  if (!path.starts_with(ClassicPrefix)) {
    return core::fail(std::format("no glyph '{}'", path));
  }
  std::string_view rest = path.substr(ClassicPrefix.size());
  const std::size_t shapeEnd = rest.find('/');
  const std::size_t frameStart = rest.rfind('/');
  if (shapeEnd == std::string_view::npos || frameStart == shapeEnd) {
    return core::fail(std::format("no glyph '{}'", path));
  }
  const std::string_view shape = rest.substr(0, shapeEnd);
  const std::string_view symbol = rest.substr(shapeEnd + 1, frameStart - shapeEnd - 1);
  const std::string_view frameText = rest.substr(frameStart + 1);
  int frame = -1;
  const auto parsed = std::from_chars(frameText.data(), frameText.data() + frameText.size(), frame);
  if (parsed.ec != std::errc{} || parsed.ptr != frameText.data() + frameText.size() || frame < 0 ||
      frame >= Frames || symbol.empty()) {
    return core::fail(std::format("no glyph '{}'", path));
  }
  if (shape == "button") {
    return classicButton(symbol, frame);
  }
  if (shape == "cap") {
    return classicCap(symbol, frame);
  }
  return core::fail(std::format("no glyph '{}'", path));
}

} // namespace dwe::ui
