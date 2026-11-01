#include <dwe/signs/sign_painter.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <utility>

namespace dwe::signs {
namespace {

// The text is drawn this many times larger than the image and averaged down, so letters a few
// pixels tall keep their shapes.
constexpr std::int32_t Supersample = 4;
// Samples per image pixel and axis when the text is mapped onto the image.
constexpr std::int32_t PixelSamples = 4;
// Coverage below this leaves a pixel as the clear made it.
constexpr float Untouched = 1.0F / 48.0F;
// The smallest font size fitting may shrink a text to, in image pixels.
constexpr float SmallestSize = 3.0F;
// How much of the box a fitted text may fill.
constexpr float FitMargin = 0.96F;
// A glow's halo is the blurred letters, this much brighter, before its strength.
constexpr float GlowGain = 2.5F;
// A diffuse clear copies the board's grain only when it has this many board pixels to take it from.
constexpr std::size_t GrainMinimumBoard = 16;
// How much of the board's grain the fill takes: a little less than all reads as the same board.
constexpr float GrainAmount = 0.7F;
// How many pixels a sprite cut from the background may sit off where its instance draws it.
constexpr std::int32_t SpriteShiftReach = 4;

struct Color {
  float red = 0.0F;
  float green = 0.0F;
  float blue = 0.0F;
};

Color colorOf(const assets::PaletteColor& color) {
  return {.red = static_cast<float>(color.red),
          .green = static_cast<float>(color.green),
          .blue = static_cast<float>(color.blue)};
}

Color mix(const Color& from, const Color& to, float amount) {
  return {.red = std::lerp(from.red, to.red, amount),
          .green = std::lerp(from.green, to.green, amount),
          .blue = std::lerp(from.blue, to.blue, amount)};
}

PixelRect intersect(const PixelRect& lhs, const PixelRect& rhs) {
  const std::int32_t left = std::max(lhs.x, rhs.x);
  const std::int32_t top = std::max(lhs.y, rhs.y);
  const std::int32_t right = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
  const std::int32_t bottom = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
  if (right <= left || bottom <= top) {
    return {};
  }
  return {.x = left, .y = top, .width = right - left, .height = bottom - top};
}

PixelRect unite(const PixelRect& lhs, const PixelRect& rhs) {
  if (lhs.width <= 0 || lhs.height <= 0) {
    return rhs;
  }
  if (rhs.width <= 0 || rhs.height <= 0) {
    return lhs;
  }
  const std::int32_t left = std::min(lhs.x, rhs.x);
  const std::int32_t top = std::min(lhs.y, rhs.y);
  return {.x = left,
          .y = top,
          .width = std::max(lhs.x + lhs.width, rhs.x + rhs.width) - left,
          .height = std::max(lhs.y + lhs.height, rhs.y + rhs.height) - top};
}

bool empty(const PixelRect& rect) {
  return rect.width <= 0 || rect.height <= 0;
}

// Colours of the image as floats over a rectangle, with the pixels a sign changed.
class Canvas {
public:
  Canvas(const assets::IndexedImage& image, std::span<const assets::PaletteColor> palette,
         PixelRect rect)
      : image_(&image), palette_(palette), rect_(rect),
        colors_(static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height)),
        changed_(colors_.size(), false) {
    for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
      for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
        colors_[slot(x, y)] = imageColor(x, y);
      }
    }
  }

  [[nodiscard]] const PixelRect& rect() const { return rect_; }
  [[nodiscard]] Color imageColor(std::int32_t x, std::int32_t y) const {
    const std::uint8_t index =
        image_
            ->indices[(static_cast<std::size_t>(y) * image_->width) + static_cast<std::size_t>(x)];
    return index < palette_.size() ? colorOf(palette_[index]) : Color{};
  }
  [[nodiscard]] Color& at(std::int32_t x, std::int32_t y) { return colors_[slot(x, y)]; }
  void set(std::int32_t x, std::int32_t y, const Color& color) {
    colors_[slot(x, y)] = color;
    changed_[slot(x, y)] = true;
  }
  [[nodiscard]] bool changed(std::int32_t x, std::int32_t y) const { return changed_[slot(x, y)]; }

private:
  [[nodiscard]] std::size_t slot(std::int32_t x, std::int32_t y) const {
    return (static_cast<std::size_t>(y - rect_.y) * static_cast<std::size_t>(rect_.width)) +
           static_cast<std::size_t>(x - rect_.x);
  }

  const assets::IndexedImage* image_;
  std::span<const assets::PaletteColor> palette_;
  PixelRect rect_;
  std::vector<Color> colors_;
  std::vector<bool> changed_;
};

// The pixels just outside a rectangle, or its own edge where the image ends.
struct Ring {
  std::int32_t left;
  std::int32_t right;
  std::int32_t top;
  std::int32_t bottom;
};

Ring ringOf(const PixelRect& rect, const assets::IndexedImage& image) {
  const auto width = static_cast<std::int32_t>(image.width);
  const auto height = static_cast<std::int32_t>(image.height);
  return {.left = rect.x > 0 ? rect.x - 1 : rect.x,
          .right = rect.x + rect.width < width ? rect.x + rect.width : rect.x + rect.width - 1,
          .top = rect.y > 0 ? rect.y - 1 : rect.y,
          .bottom =
              rect.y + rect.height < height ? rect.y + rect.height : rect.y + rect.height - 1};
}

// Whether a point is inside a quadrilateral (any winding; crossing count).
bool insideQuad(const std::array<PixelPoint, 4>& quad, float x, float y) {
  bool inside = false;
  for (std::size_t index = 0, previous = quad.size() - 1; index < quad.size(); previous = index++) {
    const PixelPoint& a = quad.at(index);
    const PixelPoint& b = quad.at(previous);
    if ((a.y > y) != (b.y > y) && x < ((b.x - a.x) * (y - a.y) / (b.y - a.y)) + a.x) {
      inside = !inside;
    }
  }
  return inside;
}

// The eye-weighted distance between two colours, on the 0-255 scale of a channel.
float distance(const Color& lhs, const Color& rhs) {
  const float red = lhs.red - rhs.red;
  const float green = lhs.green - rhs.green;
  const float blue = lhs.blue - rhs.blue;
  return std::sqrt(((2.0F * red * red) + (4.0F * green * green) + (3.0F * blue * blue)) / 9.0F);
}

// Diffuse: the pixels unlike the board are filled from the board's pixels next to them, a ring
// at a time from the outside in.
// The fill of a diffuse clear is smooth, and a smooth patch on a grainy board shows as a flat
// stain once matched to the palette: each filled pixel takes the grain of a board pixel picked
// from the area (by a fixed hash, so the result is the same on every load), its difference from
// the board's mean colour.
void addGrain(Canvas& canvas, const PixelRect& rect, const std::vector<bool>& area,
              const std::vector<bool>& removed, float tolerance) {
  std::vector<std::pair<std::int32_t, std::int32_t>> board;
  Color mean{};
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      const std::size_t at =
          (static_cast<std::size_t>(y - rect.y) * static_cast<std::size_t>(rect.width)) +
          static_cast<std::size_t>(x - rect.x);
      if (area[at] && !removed[at]) {
        board.emplace_back(x, y);
        const Color color = canvas.at(x, y);
        mean = {.red = mean.red + color.red,
                .green = mean.green + color.green,
                .blue = mean.blue + color.blue};
      }
    }
  }
  if (board.size() < GrainMinimumBoard) {
    return;
  }
  const auto count = static_cast<float>(board.size());
  mean = {.red = mean.red / count, .green = mean.green / count, .blue = mean.blue / count};
  std::vector<std::pair<std::size_t, Color>> grained;
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      const std::size_t at =
          (static_cast<std::size_t>(y - rect.y) * static_cast<std::size_t>(rect.width)) +
          static_cast<std::size_t>(x - rect.x);
      if (!removed[at]) {
        continue;
      }
      std::uint32_t hash =
          (static_cast<std::uint32_t>(x) * 73856093U) ^ (static_cast<std::uint32_t>(y) * 19349663U);
      hash ^= hash >> 13U;
      hash *= 0x5bd1e995U;
      hash ^= hash >> 15U;
      const auto [sx, sy] = board[hash % board.size()];
      const Color sample = canvas.at(sx, sy);
      if (distance(sample, mean) > tolerance) {
        // Not board grain: a frame, a crack, a leftover shadow.
        continue;
      }
      const Color base = canvas.at(x, y);
      const auto channel = [](float value) { return std::clamp(value, 0.0F, 255.0F); };
      grained.emplace_back(
          at, Color{.red = channel(base.red + (GrainAmount * (sample.red - mean.red))),
                    .green = channel(base.green + (GrainAmount * (sample.green - mean.green))),
                    .blue = channel(base.blue + (GrainAmount * (sample.blue - mean.blue)))});
    }
  }
  for (const auto& [at, color] : grained) {
    canvas.set(rect.x + static_cast<std::int32_t>(at % static_cast<std::size_t>(rect.width)),
               rect.y + static_cast<std::int32_t>(at / static_cast<std::size_t>(rect.width)),
               color);
  }
}

void diffuse(Canvas& canvas, const assets::IndexedImage& image, const SignClear& how,
             const PixelRect& rect, const std::vector<bool>& area) {
  const auto slot = [&](std::int32_t x, std::int32_t y) {
    return (static_cast<std::size_t>(y - rect.y) * static_cast<std::size_t>(rect.width)) +
           static_cast<std::size_t>(x - rect.x);
  };
  Color key{};
  if (how.color) {
    key = colorOf(*how.color);
  } else {
    std::unordered_map<std::uint8_t, std::size_t> counts;
    std::uint8_t commonest = 0;
    std::size_t most = 0;
    for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
      for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
        if (!area[slot(x, y)]) {
          continue;
        }
        const std::uint8_t index =
            image
                .indices[(static_cast<std::size_t>(y) * image.width) + static_cast<std::size_t>(x)];
        if (++counts[index] > most) {
          most = counts[index];
          commonest = index;
        }
      }
    }
    for (std::int32_t y = rect.y; y < rect.y + rect.height && most > 0; ++y) {
      for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
        if (area[slot(x, y)] && image.indices[(static_cast<std::size_t>(y) * image.width) +
                                              static_cast<std::size_t>(x)] == commonest) {
          key = canvas.imageColor(x, y);
          y = rect.y + rect.height;
          break;
        }
      }
    }
  }
  std::vector<bool> missing(area.size(), false);
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      missing[slot(x, y)] =
          area[slot(x, y)] && distance(canvas.imageColor(x, y), key) > how.tolerance;
    }
  }
  for (std::int32_t step = 0; step < how.grow; ++step) {
    std::vector<bool> grown = missing;
    for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
      for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
        if (!area[slot(x, y)] || missing[slot(x, y)]) {
          continue;
        }
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
          for (std::int32_t dx = -1; dx <= 1; ++dx) {
            const std::int32_t nx = x + dx;
            const std::int32_t ny = y + dy;
            if (nx >= rect.x && ny >= rect.y && nx < rect.x + rect.width &&
                ny < rect.y + rect.height && missing[slot(nx, ny)]) {
              grown[slot(x, y)] = true;
            }
          }
        }
      }
    }
    missing = std::move(grown);
  }
  const std::vector<bool> removed = missing;
  bool left = true;
  while (left) {
    left = false;
    std::vector<std::pair<std::size_t, Color>> filled;
    for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
      for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
        if (!missing[slot(x, y)]) {
          continue;
        }
        Color sum{};
        float count = 0.0F;
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
          for (std::int32_t dx = -1; dx <= 1; ++dx) {
            const std::int32_t nx = x + dx;
            const std::int32_t ny = y + dy;
            if (nx < rect.x || ny < rect.y || nx >= rect.x + rect.width ||
                ny >= rect.y + rect.height || !area[slot(nx, ny)] || missing[slot(nx, ny)]) {
              continue;
            }
            const Color color = canvas.at(nx, ny);
            sum = {.red = sum.red + color.red,
                   .green = sum.green + color.green,
                   .blue = sum.blue + color.blue};
            count += 1.0F;
          }
        }
        if (count > 0.0F) {
          filled.emplace_back(
              slot(x, y),
              Color{.red = sum.red / count, .green = sum.green / count, .blue = sum.blue / count});
        }
      }
    }
    if (filled.empty()) {
      // Letters that cover the whole area: the board's colour.
      for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
        for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
          if (missing[slot(x, y)]) {
            canvas.set(x, y, key);
          }
        }
      }
      break;
    }
    for (const auto& [at, color] : filled) {
      const auto x = rect.x + static_cast<std::int32_t>(at % static_cast<std::size_t>(rect.width));
      const auto y = rect.y + static_cast<std::int32_t>(at / static_cast<std::size_t>(rect.width));
      canvas.set(x, y, color);
      missing[at] = false;
    }
    left = std::ranges::contains(missing, true);
  }
  addGrain(canvas, rect, area, removed, how.tolerance);
}

void clear(Canvas& canvas, const assets::IndexedImage& image, const SignClear& how,
           const PixelRect& rect) {
  if (how.mode == SignClear::Mode::None || empty(rect)) {
    return;
  }
  // The pixels of the rectangle the clear may change: all, or those inside the quad.
  std::vector<bool> area(
      static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height), true);
  if (how.quad) {
    for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
      for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
        area[(static_cast<std::size_t>(y - rect.y) * static_cast<std::size_t>(rect.width)) +
             static_cast<std::size_t>(x - rect.x)] =
            insideQuad(*how.quad, static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F);
      }
    }
  }
  if (how.mode == SignClear::Mode::Diffuse) {
    diffuse(canvas, image, how, rect, area);
    return;
  }
  const Ring ring = ringOf(rect, image);
  if (how.mode == SignClear::Mode::Fill) {
    Color fill{};
    if (how.color) {
      fill = colorOf(*how.color);
    } else {
      Color sum{};
      float count = 0.0F;
      const auto add = [&](std::int32_t x, std::int32_t y) {
        const Color color = canvas.imageColor(x, y);
        sum = {.red = sum.red + color.red,
               .green = sum.green + color.green,
               .blue = sum.blue + color.blue};
        count += 1.0F;
      };
      for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
        add(x, ring.top);
        add(x, ring.bottom);
      }
      for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
        add(ring.left, y);
        add(ring.right, y);
      }
      fill = {.red = sum.red / count, .green = sum.green / count, .blue = sum.blue / count};
    }
    for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
      for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
        if (area[(static_cast<std::size_t>(y - rect.y) * static_cast<std::size_t>(rect.width)) +
                 static_cast<std::size_t>(x - rect.x)]) {
          canvas.set(x, y, fill);
        }
      }
    }
    return;
  }
  // Inpaint: the four pixels outside on the pixel's row and column, nearer ones weigh more.
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      const std::array<std::pair<Color, float>, 4> sides{{
          {canvas.imageColor(ring.left, y), 1.0F / static_cast<float>(x - rect.x + 1)},
          {canvas.imageColor(ring.right, y), 1.0F / static_cast<float>(rect.x + rect.width - x)},
          {canvas.imageColor(x, ring.top), 1.0F / static_cast<float>(y - rect.y + 1)},
          {canvas.imageColor(x, ring.bottom), 1.0F / static_cast<float>(rect.y + rect.height - y)},
      }};
      Color sum{};
      float weights = 0.0F;
      for (const auto& [color, weight] : sides) {
        sum = {.red = sum.red + (color.red * weight),
               .green = sum.green + (color.green * weight),
               .blue = sum.blue + (color.blue * weight)};
        weights += weight;
      }
      canvas.set(
          x, y,
          {.red = sum.red / weights, .green = sum.green / weights, .blue = sum.blue / weights});
    }
  }
}

// Grows coverage by `radius` pixels (alternating square and cross steps: an octagon).
Coverage dilate(const Coverage& source, std::int32_t radius) {
  Coverage current = source;
  for (std::int32_t step = 0; step < radius; ++step) {
    Coverage next = current;
    const bool square = step % 2 == 1;
    for (std::int32_t y = 0; y < current.height; ++y) {
      for (std::int32_t x = 0; x < current.width; ++x) {
        float value = current.at(x, y);
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
          for (std::int32_t dx = -1; dx <= 1; ++dx) {
            if (!square && dx != 0 && dy != 0) {
              continue;
            }
            value = std::max(value, current.at(x + dx, y + dy));
          }
        }
        next.values[(static_cast<std::size_t>(y) * static_cast<std::size_t>(next.width)) +
                    static_cast<std::size_t>(x)] = value;
      }
    }
    current = std::move(next);
  }
  return current;
}

// Three box blurs of `radius` along each axis: close to a Gaussian.
Coverage blur(const Coverage& source, std::int32_t radius) {
  if (radius <= 0) {
    return source;
  }
  Coverage current = source;
  const auto pass = [&](bool horizontal) {
    Coverage next = current;
    const std::int32_t length = horizontal ? current.width : current.height;
    const std::int32_t lines = horizontal ? current.height : current.width;
    const auto span = static_cast<float>((2 * radius) + 1);
    for (std::int32_t line = 0; line < lines; ++line) {
      const auto read = [&](std::int32_t along) {
        return horizontal ? current.at(along, line) : current.at(line, along);
      };
      float sum = 0.0F;
      for (std::int32_t along = -radius; along <= radius; ++along) {
        sum += read(along);
      }
      for (std::int32_t along = 0; along < length; ++along) {
        const std::int32_t x = horizontal ? along : line;
        const std::int32_t y = horizontal ? line : along;
        next.values[(static_cast<std::size_t>(y) * static_cast<std::size_t>(next.width)) +
                    static_cast<std::size_t>(x)] = sum / span;
        sum += read(along + radius + 1) - read(along - radius);
      }
    }
    current = std::move(next);
  };
  for (int round = 0; round < 3; ++round) {
    pass(true);
    pass(false);
  }
  return current;
}

float sample(const Coverage& coverage, float x, float y) {
  const float left = std::floor(x - 0.5F);
  const float top = std::floor(y - 0.5F);
  const float fx = x - 0.5F - left;
  const float fy = y - 0.5F - top;
  const auto ix = static_cast<std::int32_t>(left);
  const auto iy = static_cast<std::int32_t>(top);
  const float upper = std::lerp(coverage.at(ix, iy), coverage.at(ix + 1, iy), fx);
  const float lower = std::lerp(coverage.at(ix, iy + 1), coverage.at(ix + 1, iy + 1), fx);
  return std::lerp(upper, lower, fy);
}

std::string encodeUtf8(char32_t codePoint) {
  std::string text;
  const auto add = [&](std::uint32_t byte) { text.push_back(static_cast<char>(byte)); };
  const auto value = static_cast<std::uint32_t>(codePoint);
  if (value < 0x80U) {
    add(value);
  } else if (value < 0x800U) {
    add(0xC0U | (value >> 6U));
    add(0x80U | (value & 0x3FU));
  } else if (value < 0x10000U) {
    add(0xE0U | (value >> 12U));
    add(0x80U | ((value >> 6U) & 0x3FU));
    add(0x80U | (value & 0x3FU));
  } else {
    add(0xF0U | (value >> 18U));
    add(0x80U | ((value >> 12U) & 0x3FU));
    add(0x80U | ((value >> 6U) & 0x3FU));
    add(0x80U | (value & 0x3FU));
  }
  return text;
}

std::vector<std::string> linesOf(std::string_view text, bool vertical) {
  std::vector<std::string> lines;
  if (vertical) {
    for (const char32_t codePoint : decodeUtf8(text)) {
      if (codePoint != U'\n' && codePoint != U' ') {
        lines.push_back(encodeUtf8(codePoint));
      }
    }
    return lines;
  }
  std::size_t start = 0;
  while (true) {
    const std::size_t end = text.find('\n', start);
    lines.emplace_back(text.substr(start, end == std::string_view::npos ? end : end - start));
    if (end == std::string_view::npos) {
      return lines;
    }
    start = end + 1;
  }
}

// The text's lines drawn at a size, stacked by their baselines.
struct Block {
  std::vector<TextLine> lines;
  std::vector<float> baselines;
  float top = 0.0F;
  float bottom = 0.0F;
  float width = 0.0F;
};

Block layOut(const SignFont& font, std::span<const std::string> lines, float size,
             float letterSpacing, float lineSpacing) {
  Block block;
  const float step = font.lineHeight(size) * lineSpacing;
  bool first = true;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    TextLine line = font.drawLine(lines[index], size, letterSpacing);
    const float baseline = static_cast<float>(index) * step;
    if (line.ink.width > 0) {
      const float top = baseline - line.baseline;
      const float bottom = top + static_cast<float>(line.ink.height);
      block.top = first ? top : std::min(block.top, top);
      block.bottom = first ? bottom : std::max(block.bottom, bottom);
      block.width = std::max(block.width, static_cast<float>(line.ink.width));
      first = false;
    }
    block.lines.push_back(std::move(line));
    block.baselines.push_back(baseline);
  }
  return block;
}

// The upright text, Supersample times larger than the box, with room around it for the effects:
// box point (u, v) is coverage point ((u + pad) * Supersample, (v + pad) * Supersample).
struct Upright {
  Coverage fill;
  Coverage outline;
  Coverage glow;
  float pad = 0.0F;
};

Upright drawUpright(const Sign& sign, std::string_view text, const SignFont& font, PixelPoint box) {
  const std::vector<std::string> lines = linesOf(text, sign.vertical);
  const auto scale = static_cast<float>(Supersample);
  const float outlineWidth = sign.outline ? sign.outline->width : 0.0F;
  const float glowRadius = sign.glow ? sign.glow->radius : 0.0F;
  Upright upright;
  upright.pad = std::ceil(outlineWidth + (glowRadius * 2.0F) + 1.0F);
  const auto canvasWidth =
      static_cast<std::int32_t>(std::ceil((box.x + (2.0F * upright.pad)) * scale));
  const auto canvasHeight =
      static_cast<std::int32_t>(std::ceil((box.y + (2.0F * upright.pad)) * scale));
  upright.fill = {.width = canvasWidth,
                  .height = canvasHeight,
                  .values = std::vector<float>(static_cast<std::size_t>(canvasWidth) *
                                               static_cast<std::size_t>(canvasHeight))};

  float size = sign.size;
  Block block = layOut(font, lines, size * scale, sign.letterSpacing * scale, sign.lineSpacing);
  // What the effects add around the letters counts towards fitting.
  const float extra = 2.0F * outlineWidth * scale;
  for (int attempt = 0; sign.fit && attempt < 6; ++attempt) {
    const float width = block.width + extra;
    const float height = block.bottom - block.top + extra;
    const float shrink = std::min({1.0F, (box.x * scale * FitMargin) / std::max(width, 1.0F),
                                   (box.y * scale * FitMargin) / std::max(height, 1.0F)});
    if (shrink >= 0.995F || size <= SmallestSize) {
      break;
    }
    size = std::max(size * shrink, SmallestSize);
    block = layOut(font, lines, size * scale, sign.letterSpacing * scale, sign.lineSpacing);
  }

  const float boxLeft = upright.pad * scale;
  const float boxTop = upright.pad * scale;
  const float boxWidth = box.x * scale;
  const float boxHeight = box.y * scale;
  const float blockHeight = block.bottom - block.top;
  float blockTop = boxTop + ((boxHeight - blockHeight) / 2.0F);
  if (sign.valign == VerticalAlign::Top) {
    blockTop = boxTop + (outlineWidth * scale);
  } else if (sign.valign == VerticalAlign::Bottom) {
    blockTop = boxTop + boxHeight - blockHeight - (outlineWidth * scale);
  }
  for (std::size_t index = 0; index < block.lines.size(); ++index) {
    const TextLine& line = block.lines[index];
    if (line.ink.width == 0) {
      continue;
    }
    const auto lineWidth = static_cast<float>(line.ink.width);
    float left = boxLeft + ((boxWidth - lineWidth) / 2.0F);
    if (sign.align == HorizontalAlign::Left) {
      left = boxLeft + (outlineWidth * scale);
    } else if (sign.align == HorizontalAlign::Right) {
      left = boxLeft + boxWidth - lineWidth - (outlineWidth * scale);
    }
    const float top = blockTop + (block.baselines[index] - line.baseline - block.top);
    const auto originX = static_cast<std::int32_t>(std::lround(left));
    const auto originY = static_cast<std::int32_t>(std::lround(top));
    for (std::int32_t y = 0; y < line.ink.height; ++y) {
      for (std::int32_t x = 0; x < line.ink.width; ++x) {
        const std::int32_t cx = originX + x;
        const std::int32_t cy = originY + y;
        if (cx < 0 || cy < 0 || cx >= canvasWidth || cy >= canvasHeight) {
          continue;
        }
        float& value =
            upright.fill
                .values[(static_cast<std::size_t>(cy) * static_cast<std::size_t>(canvasWidth)) +
                        static_cast<std::size_t>(cx)];
        value = std::max(value, line.ink.at(x, y));
      }
    }
  }
  if (sign.outline) {
    upright.outline =
        dilate(upright.fill, static_cast<std::int32_t>(std::lround(sign.outline->width * scale)));
  }
  if (sign.glow) {
    Coverage glow = blur(upright.outline.width > 0 ? upright.outline : upright.fill,
                         static_cast<std::int32_t>(std::lround(sign.glow->radius * scale / 2.0F)));
    for (float& value : glow.values) {
      value = std::min(1.0F, value * GlowGain) * sign.glow->strength;
    }
    upright.glow = std::move(glow);
  }
  return upright;
}

// A projective map of the plane, row-major 3x3.
using Matrix = std::array<double, 9>;

std::optional<Matrix> invert(const Matrix& m) {
  const double a = (m[4] * m[8]) - (m[5] * m[7]);
  const double b = (m[5] * m[6]) - (m[3] * m[8]);
  const double c = (m[3] * m[7]) - (m[4] * m[6]);
  const double determinant = (m[0] * a) + (m[1] * b) + (m[2] * c);
  if (std::abs(determinant) < 1e-12) {
    return std::nullopt;
  }
  const double inverse = 1.0 / determinant;
  return Matrix{a * inverse,
                ((m[2] * m[7]) - (m[1] * m[8])) * inverse,
                ((m[1] * m[5]) - (m[2] * m[4])) * inverse,
                b * inverse,
                ((m[0] * m[8]) - (m[2] * m[6])) * inverse,
                ((m[2] * m[3]) - (m[0] * m[5])) * inverse,
                c * inverse,
                ((m[1] * m[6]) - (m[0] * m[7])) * inverse,
                ((m[0] * m[4]) - (m[1] * m[3])) * inverse};
}

// The map of the unit square's corners (0,0), (1,0), (1,1), (0,1) onto a quadrilateral
// (Heckbert's square-to-quad).
Matrix squareToQuad(const std::array<PixelPoint, 4>& quad) {
  const double x0 = quad[0].x;
  const double y0 = quad[0].y;
  const double x1 = quad[1].x;
  const double y1 = quad[1].y;
  const double x2 = quad[2].x;
  const double y2 = quad[2].y;
  const double x3 = quad[3].x;
  const double y3 = quad[3].y;
  const double sx = x0 - x1 + x2 - x3;
  const double sy = y0 - y1 + y2 - y3;
  if (std::abs(sx) < 1e-9 && std::abs(sy) < 1e-9) {
    return {x1 - x0, x3 - x0, x0, y1 - y0, y3 - y0, y0, 0.0, 0.0, 1.0};
  }
  const double dx1 = x1 - x2;
  const double dx2 = x3 - x2;
  const double dy1 = y1 - y2;
  const double dy2 = y3 - y2;
  const double determinant = (dx1 * dy2) - (dx2 * dy1);
  const double g = ((sx * dy2) - (dx2 * sy)) / determinant;
  const double h = ((dx1 * sy) - (sx * dy1)) / determinant;
  return {x1 - x0 + (g * x1),
          x3 - x0 + (h * x3),
          x0,
          y1 - y0 + (g * y1),
          y3 - y0 + (h * y3),
          y0,
          g,
          h,
          1.0};
}

// Where the text box's corners land in the image, and the box's size.
std::pair<std::array<PixelPoint, 4>, PixelPoint> placeBox(const Sign& sign) {
  const PixelRect& region = sign.region;
  if (sign.corners) {
    const auto& c = *sign.corners;
    const auto distance = [](PixelPoint a, PixelPoint b) {
      return std::hypot(a.x - b.x, a.y - b.y);
    };
    const PixelPoint size = sign.box.value_or(PixelPoint{
        .x = (distance(c[0], c[1]) + distance(c[3], c[2])) / 2.0F,
        .y = (distance(c[0], c[3]) + distance(c[1], c[2])) / 2.0F,
    });
    return {c, size};
  }
  const PixelPoint size = sign.box.value_or(
      PixelPoint{.x = static_cast<float>(region.width), .y = static_cast<float>(region.height)});
  const float centerX = static_cast<float>(region.x) + (static_cast<float>(region.width) / 2.0F);
  const float centerY = static_cast<float>(region.y) + (static_cast<float>(region.height) / 2.0F);
  const float angle = sign.rotation * std::numbers::pi_v<float> / 180.0F;
  const float lean = std::tan(sign.skew * std::numbers::pi_v<float> / 180.0F);
  const auto place = [&](float x, float y) {
    const float leaned = x - (lean * y);
    return PixelPoint{.x = centerX + (leaned * std::cos(angle)) - (y * std::sin(angle)),
                      .y = centerY + (leaned * std::sin(angle)) + (y * std::cos(angle))};
  };
  const float halfWidth = size.x / 2.0F;
  const float halfHeight = size.y / 2.0F;
  return {{place(-halfWidth, -halfHeight), place(halfWidth, -halfHeight),
           place(halfWidth, halfHeight), place(-halfWidth, halfHeight)},
          size};
}

// Picks palette indices for colours: the nearest allowed colour, weighted for the eye.
class Quantizer {
public:
  Quantizer(std::span<const assets::PaletteColor> palette, std::vector<std::uint8_t> allowed)
      : palette_(palette), allowed_(std::move(allowed)) {}

  [[nodiscard]] bool usable() const { return !allowed_.empty(); }

  std::uint8_t nearest(const Color& color) {
    const auto key = (static_cast<std::uint32_t>(std::clamp(color.red, 0.0F, 255.0F)) << 16U) |
                     (static_cast<std::uint32_t>(std::clamp(color.green, 0.0F, 255.0F)) << 8U) |
                     static_cast<std::uint32_t>(std::clamp(color.blue, 0.0F, 255.0F));
    if (const auto found = cache_.find(key); found != cache_.end()) {
      return found->second;
    }
    std::uint8_t best = allowed_.front();
    float bestDistance = std::numeric_limits<float>::max();
    for (const std::uint8_t index : allowed_) {
      const Color candidate = colorOf(palette_[index]);
      const float red = candidate.red - color.red;
      const float green = candidate.green - color.green;
      const float blue = candidate.blue - color.blue;
      const float distance = (2.0F * red * red) + (4.0F * green * green) + (3.0F * blue * blue);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = index;
      }
    }
    cache_.emplace(key, best);
    return best;
  }

private:
  std::span<const assets::PaletteColor> palette_;
  std::vector<std::uint8_t> allowed_;
  std::unordered_map<std::uint32_t, std::uint8_t> cache_;
};

std::vector<std::uint8_t> allowedIndices(const assets::IndexedImage& image,
                                         std::span<const assets::PaletteColor> palette,
                                         const PixelRect& around, SignColors colors) {
  const std::size_t count = std::min<std::size_t>(palette.size(), 256);
  std::vector<bool> present(count, false);
  const PixelRect bounds{.x = 0,
                         .y = 0,
                         .width = static_cast<std::int32_t>(image.width),
                         .height = static_cast<std::int32_t>(image.height)};
  const PixelRect ring = intersect({.x = around.x - 1,
                                    .y = around.y - 1,
                                    .width = around.width + 2,
                                    .height = around.height + 2},
                                   bounds);
  for (std::int32_t y = ring.y; y < ring.y + ring.height; ++y) {
    for (std::int32_t x = ring.x; x < ring.x + ring.width; ++x) {
      const std::uint8_t index =
          image.indices[(static_cast<std::size_t>(y) * image.width) + static_cast<std::size_t>(x)];
      if (index < count) {
        present[index] = true;
      }
    }
  }
  std::vector<std::uint8_t> allowed;
  for (std::size_t index = 0; index < count; ++index) {
    const std::uint8_t alpha = palette[index].alpha;
    const bool keep = colors == SignColors::Region ? present[index] && alpha != 0
                                                   : alpha == 255 || (present[index] && alpha != 0);
    if (keep) {
      allowed.push_back(static_cast<std::uint8_t>(index));
    }
  }
  return allowed;
}

} // namespace

PixelRect paintSign(assets::IndexedImage& image, std::span<const assets::PaletteColor> palette,
                    const Sign& sign, std::string_view text, const SignFont& font,
                    const assets::IndexedImage* original) {
  const PixelRect bounds{.x = 0,
                         .y = 0,
                         .width = static_cast<std::int32_t>(image.width),
                         .height = static_cast<std::int32_t>(image.height)};
  const PixelRect region = intersect(sign.region, bounds);
  PixelRect clearRect = sign.clear.rect.value_or(sign.region);
  if (sign.clear.quad && !sign.clear.rect) {
    const auto& quad = *sign.clear.quad;
    const auto [left, right] = std::ranges::minmax({quad[0].x, quad[1].x, quad[2].x, quad[3].x});
    const auto [top, bottom] = std::ranges::minmax({quad[0].y, quad[1].y, quad[2].y, quad[3].y});
    clearRect = {.x = static_cast<std::int32_t>(std::floor(left)),
                 .y = static_cast<std::int32_t>(std::floor(top)),
                 .width = static_cast<std::int32_t>(std::ceil(right) - std::floor(left)),
                 .height = static_cast<std::int32_t>(std::ceil(bottom) - std::floor(top))};
  }
  const PixelRect cleared = intersect(clearRect, bounds);
  const PixelRect touched = unite(region, cleared);
  if (empty(touched) || palette.empty()) {
    return {};
  }
  const bool sameSize =
      original != nullptr && original->width == image.width && original->height == image.height;
  Quantizer quantizer{palette,
                      allowedIndices(sameSize ? *original : image, palette, touched, sign.colors)};
  if (!quantizer.usable()) {
    return {};
  }
  Canvas canvas{image, palette, touched};
  clear(canvas, image, sign.clear, cleared);

  const auto [corners, box] = placeBox(sign);
  const std::optional<Matrix> toBox = invert(squareToQuad(corners));
  if (toBox && !empty(region) && !text.empty()) {
    const Upright upright = drawUpright(sign, text, font, box);
    const auto scale = static_cast<float>(Supersample);
    const Color fill = colorOf(sign.color);
    const float fillAlpha = static_cast<float>(sign.color.alpha) / 255.0F;
    for (std::int32_t y = region.y; y < region.y + region.height; ++y) {
      for (std::int32_t x = region.x; x < region.x + region.width; ++x) {
        float letters = 0.0F;
        float outline = 0.0F;
        float glow = 0.0F;
        for (std::int32_t sy = 0; sy < PixelSamples; ++sy) {
          for (std::int32_t sx = 0; sx < PixelSamples; ++sx) {
            const double px = x + ((sx + 0.5) / PixelSamples);
            const double py = y + ((sy + 0.5) / PixelSamples);
            const Matrix& m = *toBox;
            const double w = (m[6] * px) + (m[7] * py) + m[8];
            if (std::abs(w) < 1e-12) {
              continue;
            }
            // Unit square coordinates, then the upright coverage's.
            const auto u = static_cast<float>(((m[0] * px) + (m[1] * py) + m[2]) / w);
            const auto v = static_cast<float>(((m[3] * px) + (m[4] * py) + m[5]) / w);
            const float cx = ((u * box.x) + upright.pad) * scale;
            const float cy = ((v * box.y) + upright.pad) * scale;
            letters += sample(upright.fill, cx, cy);
            if (upright.outline.width > 0) {
              outline += sample(upright.outline, cx, cy);
            }
            if (upright.glow.width > 0) {
              glow += sample(upright.glow, cx, cy);
            }
          }
        }
        constexpr auto Samples = static_cast<float>(PixelSamples * PixelSamples);
        letters /= Samples;
        outline /= Samples;
        glow /= Samples;
        if (std::max({letters, outline, glow}) < Untouched) {
          continue;
        }
        Color color = canvas.at(x, y);
        if (sign.glow) {
          color = mix(color, colorOf(sign.glow->color), std::clamp(glow, 0.0F, 1.0F));
        }
        if (sign.outline) {
          color = mix(color, colorOf(sign.outline->color),
                      std::clamp(outline, 0.0F, 1.0F) *
                          (static_cast<float>(sign.outline->color.alpha) / 255.0F));
        }
        color = mix(color, fill, std::clamp(letters, 0.0F, 1.0F) * fillAlpha);
        canvas.set(x, y, color);
      }
    }
  }

  PixelRect changed{};
  for (std::int32_t y = touched.y; y < touched.y + touched.height; ++y) {
    for (std::int32_t x = touched.x; x < touched.x + touched.width; ++x) {
      if (!canvas.changed(x, y)) {
        continue;
      }
      image.indices[(static_cast<std::size_t>(y) * image.width) + static_cast<std::size_t>(x)] =
          quantizer.nearest(canvas.at(x, y));
      changed = unite(changed, {.x = x, .y = y, .width = 1, .height = 1});
    }
  }
  return changed;
}

assets::MapTexturePage* findSignPage(std::span<assets::MapTexturePage> pages,
                                     std::int32_t bitsPerPixel, std::int32_t page) {
  const std::int32_t width = bitsPerPixel == 4 ? assets::MapVramLayout::FourBitPageWidth
                                               : assets::MapVramLayout::EightBitPageWidth;
  const std::int32_t vramX = assets::MapVramLayout::FirstPageX + (page * width);
  const auto found = std::ranges::find_if(pages, [&](const assets::MapTexturePage& candidate) {
    return candidate.bitsPerPixel == bitsPerPixel && candidate.vramX == vramX;
  });
  return found == pages.end() ? nullptr : &*found;
}

const assets::MapTexturePage* findSignPage(std::span<const assets::MapTexturePage> pages,
                                           std::int32_t bitsPerPixel, std::int32_t page) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the same search, read only.
  return findSignPage(std::span{const_cast<assets::MapTexturePage*>(pages.data()), pages.size()},
                      bitsPerPixel, page);
}

// How far from where an instance draws a sprite the background holds the sprite's texels: the
// shift (up to SpriteShiftReach pixels each way) with the most equal indices, none on a tie.
glm::ivec2 spriteShift(const assets::IndexedImage& background, const assets::IndexedImage& page,
                       const assets::MapSprite& sprite, const assets::MapObjectInstance& instance) {
  const std::int32_t baseX = sprite.texX % assets::MapPageTexels;
  const std::int32_t baseY = sprite.texY % assets::MapPageTexels;
  glm::ivec2 best{0, 0};
  std::size_t bestCount = 0;
  for (std::int32_t dy = -SpriteShiftReach; dy <= SpriteShiftReach; ++dy) {
    for (std::int32_t dx = -SpriteShiftReach; dx <= SpriteShiftReach; ++dx) {
      std::size_t count = 0;
      for (std::int32_t y = 0; y < sprite.height; ++y) {
        const std::int32_t backgroundY = instance.y + dy + y;
        const std::int32_t texelY = baseY + y;
        if (backgroundY < 0 || std::cmp_greater_equal(backgroundY, background.height) ||
            std::cmp_greater_equal(texelY, page.height)) {
          continue;
        }
        for (std::int32_t x = 0; x < sprite.width; ++x) {
          const std::int32_t backgroundX = instance.x + dx + x;
          const std::int32_t texelX = baseX + x;
          if (backgroundX < 0 || std::cmp_greater_equal(backgroundX, background.width) ||
              std::cmp_greater_equal(texelX, page.width)) {
            continue;
          }
          count += background.indices[(static_cast<std::size_t>(backgroundY) * background.width) +
                                      static_cast<std::size_t>(backgroundX)] ==
                   page.indices[(static_cast<std::size_t>(texelY) * page.width) +
                                static_cast<std::size_t>(texelX)];
        }
      }
      const bool closer = std::abs(dx) + std::abs(dy) < std::abs(best.x) + std::abs(best.y);
      if (count > bestCount || (count == bestCount && closer)) {
        bestCount = count;
        best = {dx, dy};
      }
    }
  }
  return best;
}

// The background pixels a sign changed, written into the sprites that show the background there:
// 8-bit sprites in the background's palette (clut -1) whose texel, where an instance places it,
// holds the same index as the background did. Each page touched gets a patch.
void mirrorIntoSprites(const assets::MapImages& images, assets::MapImages& working,
                       const PixelRect& changed, const assets::MapObjects& objects,
                       std::vector<SignPatch>& patches) {
  const auto at = [](const assets::IndexedImage& image, std::int32_t x, std::int32_t y) {
    return image.indices[(static_cast<std::size_t>(y) * image.width) + static_cast<std::size_t>(x)];
  };
  const auto inside = [](const assets::IndexedImage& image, std::int32_t x, std::int32_t y) {
    return x >= 0 && y >= 0 && std::cmp_less(x, image.width) && std::cmp_less(y, image.height);
  };
  const std::vector<assets::PaletteColor>& palette = images.backgroundPalettes.front();
  for (const assets::MapObjectInstance& instance : objects.instances) {
    if ((instance.flag & assets::MapObjectInstance::ScreenSpaceFlag) != 0) {
      continue;
    }
    std::vector<std::int32_t> shown;
    for (const assets::MapSpriteFrame& frame : instance.frames) {
      if (frame.sprite >= 0 && std::cmp_less(frame.sprite, objects.sprites.size()) &&
          !std::ranges::contains(shown, frame.sprite)) {
        shown.push_back(frame.sprite);
      }
    }
    for (const std::int32_t index : shown) {
      const assets::MapSprite& sprite = objects.sprites[static_cast<std::size_t>(index)];
      if (sprite.clut != -1) {
        continue;
      }
      const std::int32_t number = sprite.texX / assets::MapPageTexels;
      assets::MapTexturePage* page = findSignPage(std::span{working.pages}, 8, number);
      const assets::MapTexturePage* source = findSignPage(std::span{images.pages}, 8, number);
      if (page == nullptr || source == nullptr) {
        continue;
      }
      if (instance.x >= changed.x + changed.width || instance.x + sprite.width <= changed.x ||
          instance.y >= changed.y + changed.height || instance.y + sprite.height <= changed.y) {
        continue;
      }
      // Some sprites were cut a pixel or two off where the instance draws them: the shift at
      // which their texels match the background best says where each texel came from.
      const glm::ivec2 shift = spriteShift(images.background, source->image, sprite, instance);
      const std::int32_t originX = instance.x + shift.x;
      const std::int32_t originY = instance.y + shift.y;
      const std::int32_t left = std::max(changed.x, originX);
      const std::int32_t top = std::max(changed.y, originY);
      const std::int32_t right = std::min(changed.x + changed.width, originX + sprite.width);
      const std::int32_t bottom = std::min(changed.y + changed.height, originY + sprite.height);
      PixelRect touched;
      for (std::int32_t y = top; y < bottom; ++y) {
        for (std::int32_t x = left; x < right; ++x) {
          const std::uint8_t before = at(images.background, x, y);
          const std::uint8_t after = at(working.background, x, y);
          const std::int32_t texelX = (sprite.texX % assets::MapPageTexels) + (x - originX);
          const std::int32_t texelY = (sprite.texY % assets::MapPageTexels) + (y - originY);
          if (before == after || before >= palette.size() || palette[before].alpha == 0 ||
              !inside(source->image, texelX, texelY) ||
              at(source->image, texelX, texelY) != before) {
            continue;
          }
          page->image.indices[(static_cast<std::size_t>(texelY) * page->image.width) +
                              static_cast<std::size_t>(texelX)] = after;
          touched = empty(touched)
                        ? PixelRect{.x = texelX, .y = texelY, .width = 1, .height = 1}
                        : unite(touched, {.x = texelX, .y = texelY, .width = 1, .height = 1});
        }
      }
      if (empty(touched)) {
        continue;
      }
      SignPatch patch{.kind = SignTarget::Kind::Page,
                      .bitsPerPixel = 8,
                      .page = number,
                      .rect = touched,
                      .indices = {}};
      for (std::int32_t y = touched.y; y < touched.y + touched.height; ++y) {
        for (std::int32_t x = touched.x; x < touched.x + touched.width; ++x) {
          patch.indices.push_back(at(page->image, x, y));
        }
      }
      patches.push_back(std::move(patch));
    }
  }
}

PaintedSigns paintSigns(const assets::MapImages& images, std::span<const Sign> signs,
                        const SignResources& resources, const assets::MapObjects* objects) {
  PaintedSigns painted;
  assets::MapImages working = images;
  for (const Sign& sign : signs) {
    const SignFont* font = resources.font ? resources.font(sign.font) : nullptr;
    if (font == nullptr) {
      painted.warnings.push_back(std::format("sign {}: no font {}", sign.id, sign.font));
      continue;
    }
    assets::IndexedImage* image = &working.background;
    const assets::IndexedImage* original = &images.background;
    const std::vector<assets::PaletteColor>* palette = nullptr;
    if (sign.target.kind == SignTarget::Kind::Page) {
      assets::MapTexturePage* page =
          findSignPage(std::span{working.pages}, sign.target.bitsPerPixel, sign.target.page);
      if (page == nullptr) {
        painted.warnings.push_back(std::format("sign {}: the map has no {}-bit page {}", sign.id,
                                               sign.target.bitsPerPixel, sign.target.page));
        continue;
      }
      image = &page->image;
      original =
          &findSignPage(std::span{images.pages}, sign.target.bitsPerPixel, sign.target.page)->image;
    }
    palette = assets::mapClutPalette(sign.target.clut, working.pages, working.backgroundPalettes);
    if (palette == nullptr) {
      painted.warnings.push_back(
          std::format("sign {}: no palette at clut {}", sign.id, sign.target.clut));
      continue;
    }
    const std::string text = resources.text ? resources.text(sign.text) : sign.text;
    const PixelRect changed = paintSign(*image, *palette, sign, text, *font, original);
    if (empty(changed)) {
      continue;
    }
    SignPatch patch{.kind = sign.target.kind,
                    .bitsPerPixel = sign.target.bitsPerPixel,
                    .page = sign.target.page,
                    .rect = changed,
                    .indices = {}};
    patch.indices.reserve(static_cast<std::size_t>(changed.width) *
                          static_cast<std::size_t>(changed.height));
    for (std::int32_t y = changed.y; y < changed.y + changed.height; ++y) {
      for (std::int32_t x = changed.x; x < changed.x + changed.width; ++x) {
        patch.indices.push_back(image->indices[(static_cast<std::size_t>(y) * image->width) +
                                               static_cast<std::size_t>(x)]);
      }
    }
    painted.patches.push_back(std::move(patch));
    if (objects != nullptr && sign.target.kind == SignTarget::Kind::Background &&
        !images.backgroundPalettes.empty()) {
      mirrorIntoSprites(images, working, changed, *objects, painted.patches);
    }
  }
  return painted;
}

void applySignPatches(assets::MapImages& images, std::span<const SignPatch> patches,
                      std::span<const assets::MapSprite> sprites) {
  bool pagesChanged = false;
  for (const SignPatch& patch : patches) {
    assets::IndexedImage* image = &images.background;
    if (patch.kind == SignTarget::Kind::Page) {
      assets::MapTexturePage* page =
          findSignPage(std::span{images.pages}, patch.bitsPerPixel, patch.page);
      if (page == nullptr) {
        continue;
      }
      image = &page->image;
      pagesChanged = true;
    }
    std::size_t next = 0;
    for (std::int32_t y = patch.rect.y; y < patch.rect.y + patch.rect.height; ++y) {
      for (std::int32_t x = patch.rect.x; x < patch.rect.x + patch.rect.width; ++x) {
        if (x >= 0 && y >= 0 && std::cmp_less(x, image->width) && std::cmp_less(y, image->height) &&
            next < patch.indices.size()) {
          image->indices[(static_cast<std::size_t>(y) * image->width) +
                         static_cast<std::size_t>(x)] = patch.indices[next];
        }
        ++next;
      }
    }
  }
  if (pagesChanged) {
    images.sprites = assets::buildMapSpriteSheet(sprites, images.pages, images.backgroundPalettes);
  }
}

} // namespace dwe::signs
