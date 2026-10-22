#include "index_scale.hpp"

#include <dwe/core/message.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::dwx {

namespace {

constexpr std::string_view EdgeModeName = "edge";
constexpr std::string_view NearestModeName = "nearest";

// The factors the edge rules exist for, and the order they compose larger factors in.
constexpr std::uint32_t EdgeDouble = 2;
constexpr std::uint32_t EdgeTriple = 3;
// The factors edge reaches: 1, the rules themselves, and their compositions within the maximum (4 =
// 2·2, 6 = 2·3, 8 = 2·2·2).
constexpr std::array<std::uint32_t, 6> EdgeFactors{1, 2, 3, 4, 6, 8};

// The 3 x 3 neighbourhood of a pixel, row by row; past the image's edge the border pixel repeats.
struct Neighbourhood {
  std::uint8_t upLeft = 0;
  std::uint8_t up = 0;
  std::uint8_t upRight = 0;
  std::uint8_t left = 0;
  std::uint8_t centre = 0;
  std::uint8_t right = 0;
  std::uint8_t downLeft = 0;
  std::uint8_t down = 0;
  std::uint8_t downRight = 0;
};

std::uint8_t indexAt(const assets::IndexedImage& image, std::int64_t x, std::int64_t y) {
  const auto column = static_cast<std::size_t>(std::clamp<std::int64_t>(x, 0, image.width - 1));
  const auto row = static_cast<std::size_t>(std::clamp<std::int64_t>(y, 0, image.height - 1));
  return image.indices[(row * image.width) + column];
}

Neighbourhood neighbourhoodOf(const assets::IndexedImage& image, std::uint32_t x, std::uint32_t y) {
  const std::int64_t column = x;
  const std::int64_t row = y;
  return Neighbourhood{
      .upLeft = indexAt(image, column - 1, row - 1),
      .up = indexAt(image, column, row - 1),
      .upRight = indexAt(image, column + 1, row - 1),
      .left = indexAt(image, column - 1, row),
      .centre = indexAt(image, column, row),
      .right = indexAt(image, column + 1, row),
      .downLeft = indexAt(image, column - 1, row + 1),
      .down = indexAt(image, column, row + 1),
      .downRight = indexAt(image, column + 1, row + 1),
  };
}

// A pixel split into 2 x 2: a corner takes the colour of the two neighbours that meet at it when
// they agree and the edge between them is not a straight line through the pixel.
std::array<std::uint8_t, 4> doubled(const Neighbourhood& at) {
  const std::uint8_t centre = at.centre;
  if (at.up == at.down || at.left == at.right) {
    return {centre, centre, centre, centre};
  }
  return {
      at.left == at.up ? at.left : centre,
      at.up == at.right ? at.right : centre,
      at.left == at.down ? at.left : centre,
      at.down == at.right ? at.right : centre,
  };
}

// A pixel split into 3 x 3 by the same idea: corners as in doubled, and each edge cell takes its
// side's colour when a corner rule reaches it without cutting through a matching diagonal
// neighbour.
std::array<std::uint8_t, 9> tripled(const Neighbourhood& at) {
  const std::uint8_t centre = at.centre;
  std::array<std::uint8_t, 9> cells{};
  cells.fill(centre);
  if (at.up == at.down || at.left == at.right) {
    return cells;
  }
  const bool upLeftMeets = at.left == at.up;
  const bool upRightMeets = at.up == at.right;
  const bool downLeftMeets = at.left == at.down;
  const bool downRightMeets = at.down == at.right;
  cells[0] = upLeftMeets ? at.left : centre;
  cells[1] = (upLeftMeets && centre != at.upRight) || (upRightMeets && centre != at.upLeft)
                 ? at.up
                 : centre;
  cells[2] = upRightMeets ? at.right : centre;
  cells[3] = (upLeftMeets && centre != at.downLeft) || (downLeftMeets && centre != at.upLeft)
                 ? at.left
                 : centre;
  cells[5] = (upRightMeets && centre != at.downRight) || (downRightMeets && centre != at.upRight)
                 ? at.right
                 : centre;
  cells[6] = downLeftMeets ? at.left : centre;
  cells[7] = (downLeftMeets && centre != at.downRight) || (downRightMeets && centre != at.downLeft)
                 ? at.down
                 : centre;
  cells[8] = downRightMeets ? at.right : centre;
  return cells;
}

assets::IndexedImage emptyGrown(const assets::IndexedImage& image, std::uint32_t factor) {
  assets::IndexedImage grown{
      .width = image.width * factor,
      .height = image.height * factor,
      .indices = {},
      .palette = image.palette,
  };
  grown.indices.resize(static_cast<std::size_t>(grown.width) * grown.height);
  return grown;
}

// Writes the factor x factor cells of the source pixel (x, y), row by row, into the grown image.
void placeCells(assets::IndexedImage& grown, std::uint32_t x, std::uint32_t y, std::uint32_t factor,
                std::span<const std::uint8_t> cells) {
  for (std::uint32_t row = 0; row < factor; ++row) {
    const std::size_t start = (static_cast<std::size_t>((y * factor) + row) * grown.width) +
                              (static_cast<std::size_t>(x) * factor);
    std::ranges::copy(cells.subspan(static_cast<std::size_t>(row) * factor, factor),
                      grown.indices.begin() + static_cast<std::ptrdiff_t>(start));
  }
}

assets::IndexedImage edgeStep(const assets::IndexedImage& image, std::uint32_t factor) {
  assets::IndexedImage grown = emptyGrown(image, factor);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const Neighbourhood at = neighbourhoodOf(image, x, y);
      if (factor == EdgeDouble) {
        placeCells(grown, x, y, factor, doubled(at));
      } else {
        placeCells(grown, x, y, factor, tripled(at));
      }
    }
  }
  return grown;
}

assets::IndexedImage nearest(const assets::IndexedImage& image, std::uint32_t factor) {
  assets::IndexedImage grown = emptyGrown(image, factor);
  std::vector<std::uint8_t> cells(static_cast<std::size_t>(factor) * factor);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      std::ranges::fill(cells, image.indices[(static_cast<std::size_t>(y) * image.width) + x]);
      placeCells(grown, x, y, factor, cells);
    }
  }
  return grown;
}

// The edge steps a factor composes into, in order: 2s first, then a 3.
std::vector<std::uint32_t> edgeStepsOf(std::uint32_t factor) {
  std::vector<std::uint32_t> steps;
  while (factor % EdgeDouble == 0) {
    steps.push_back(EdgeDouble);
    factor /= EdgeDouble;
  }
  if (factor == EdgeTriple) {
    steps.push_back(EdgeTriple);
  }
  return steps;
}

} // namespace

std::optional<IndexScaleMode> indexScaleModeNamed(std::string_view name) {
  if (name == EdgeModeName) {
    return IndexScaleMode::Edge;
  }
  if (name == NearestModeName) {
    return IndexScaleMode::Nearest;
  }
  return std::nullopt;
}

bool supportsIndexScale(IndexScaleMode mode, std::uint32_t factor) {
  if (factor < MinIndexScaleFactor || factor > MaxIndexScaleFactor) {
    return false;
  }
  return mode == IndexScaleMode::Nearest || std::ranges::contains(EdgeFactors, factor);
}

core::Result<assets::IndexedImage> indexScale(const assets::IndexedImage& image,
                                              std::uint32_t factor, IndexScaleMode mode) {
  if (!supportsIndexScale(mode, factor)) {
    return core::fail(
        core::formatMessage(messages::IndexScaleUnsupported, factor,
                            mode == IndexScaleMode::Edge ? EdgeModeName : NearestModeName));
  }
  if (image.indices.size() != static_cast<std::size_t>(image.width) * image.height) {
    return core::fail(core::formatMessage(messages::IndexScaleBadImage, image.width, image.height,
                                          image.indices.size()));
  }
  if (factor == 1) {
    return image;
  }
  if (mode == IndexScaleMode::Nearest) {
    return nearest(image, factor);
  }
  assets::IndexedImage grown = image;
  for (const std::uint32_t step : edgeStepsOf(factor)) {
    grown = edgeStep(grown, step);
  }
  return grown;
}

} // namespace dwe::dwx
