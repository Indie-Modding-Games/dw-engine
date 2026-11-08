#include <dwe/formats/video/restore.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace dwe::formats::video {

namespace {

constexpr std::size_t BlockSide = 8;
// Annex F.3.1: a difference at most THR1 counts as flat; THR2 flat differences make a flat line.
constexpr int FlatStep = 2 * PreciseOne;
constexpr int FlatCount = 6;

// One plane of a frame, as signed samples.
struct Plane {
  std::vector<std::int16_t>* samples;
  std::size_t width;
  std::size_t height;
};

// Integer division rounded down, for negative numerators too.
int floorDiv(int numerator, int denominator) {
  const int quotient = numerator / denominator;
  return (numerator % denominator != 0 && (numerator < 0) != (denominator < 0)) ? quotient - 1
                                                                                : quotient;
}

int sign(int value) {
  return (value > 0) - (value < 0);
}

// Filters the ten samples v[0..9] across one edge (between v[4] and v[5]) in place.
void filterEdge(std::array<int, 10>& v, int qp) {
  int flat = 0;
  for (std::size_t i = 0; i + 1 < v.size(); ++i) {
    flat += std::abs(v[i] - v[i + 1]) <= FlatStep ? 1 : 0;
  }
  if (flat >= FlatCount) {
    // DC offset mode: a flat line whose step is within the quantisation gets a 9-tap low-pass.
    const auto [low, high] = std::minmax_element(v.begin() + 1, v.begin() + 9);
    if (*high - *low >= 2 * qp) {
      return;
    }
    std::array<int, 18> padded{};
    const int left = std::abs(v[1] - v[0]) < qp ? v[0] : v[1];
    const int right = std::abs(v[8] - v[9]) < qp ? v[9] : v[8];
    for (int i = 0; i < 18; ++i) {
      const int m = i - 4; // p_m for m = -4..13
      padded[static_cast<std::size_t>(i)] =
          m < 1 ? left : (m > 8 ? right : v[static_cast<std::size_t>(m)]);
    }
    constexpr std::array<int, 9> Taps = {1, 1, 2, 2, 4, 2, 2, 1, 1};
    std::array<int, 10> out = v;
    for (std::size_t n = 1; n <= 8; ++n) {
      int sum = 0;
      for (std::size_t k = 0; k < Taps.size(); ++k) {
        sum += Taps[k] * padded[n + k];
      }
      out[n] = (sum + 8) >> 4;
    }
    v = out;
    return;
  }
  // Default mode: the edge's step (its third-order term) brought down to the smallest of its
  // neighbours', when it is smaller than the quantiser.
  const int a0 = floorDiv((2 * v[3]) - (5 * v[4]) + (5 * v[5]) - (2 * v[6]), 8);
  if (std::abs(a0) >= qp) {
    return;
  }
  const int a1 = floorDiv((2 * v[1]) - (5 * v[2]) + (5 * v[3]) - (2 * v[4]), 8);
  const int a2 = floorDiv((2 * v[5]) - (5 * v[6]) + (5 * v[7]) - (2 * v[8]), 8);
  const int smallest = sign(a0) * std::min({std::abs(a0), std::abs(a1), std::abs(a2)});
  const int half = floorDiv(v[4] - v[5], 2);
  const int d = std::clamp(floorDiv(5 * (smallest - a0), 8), std::min(0, half), std::max(0, half));
  v[4] -= d;
  v[5] += d;
}

void deblockPlane(const Plane& plane, int qp) {
  auto& s = *plane.samples;
  std::array<int, 10> line{};
  // Vertical edges: each row across them.
  for (std::size_t y = 0; y < plane.height; ++y) {
    for (std::size_t edge = BlockSide; edge + BlockSide <= plane.width; edge += BlockSide) {
      const std::size_t start = (y * plane.width) + edge - 5;
      for (std::size_t i = 0; i < line.size(); ++i) {
        line[i] = s[start + i];
      }
      filterEdge(line, qp);
      for (std::size_t i = 0; i < line.size(); ++i) {
        s[start + i] = static_cast<std::int16_t>(std::clamp(line[i], 0, PreciseMax));
      }
    }
  }
  // Horizontal edges: each column across them.
  for (std::size_t x = 0; x < plane.width; ++x) {
    for (std::size_t edge = BlockSide; edge + BlockSide <= plane.height; edge += BlockSide) {
      const std::size_t start = ((edge - 5) * plane.width) + x;
      for (std::size_t i = 0; i < line.size(); ++i) {
        line[i] = s[start + (i * plane.width)];
      }
      filterEdge(line, qp);
      for (std::size_t i = 0; i < line.size(); ++i) {
        s[start + (i * plane.width)] =
            static_cast<std::int16_t>(std::clamp(line[i], 0, PreciseMax));
      }
    }
  }
}

void deringPlane(const Plane& plane, int qp) {
  const std::vector<std::int16_t> source = *plane.samples;
  auto& out = *plane.samples;
  const auto width = static_cast<int>(plane.width);
  const auto height = static_cast<int>(plane.height);
  const auto at = [&](int x, int y) {
    return int{source[(static_cast<std::size_t>(std::clamp(y, 0, height - 1)) * plane.width) +
                      static_cast<std::size_t>(std::clamp(x, 0, width - 1))]};
  };
  const int limit = qp / 2;
  // A block whose levels span less than this has no edge to ring around.
  constexpr int MinRange = 16 * PreciseOne;
  constexpr int Side = static_cast<int>(BlockSide);
  for (int top = 0; top < height; top += Side) {
    for (int left = 0; left < width; left += Side) {
      int low = PreciseMax;
      int high = 0;
      for (int y = top; y < top + Side; ++y) {
        for (int x = left; x < left + Side; ++x) {
          low = std::min(low, at(x, y));
          high = std::max(high, at(x, y));
        }
      }
      if (high - low < MinRange) {
        continue;
      }
      const int threshold = (high + low + 1) / 2;
      // Which side of the mid level each sample of the block and its one-sample border is on.
      std::array<std::array<bool, BlockSide + 2>, BlockSide + 2> above{};
      for (int y = -1; y <= Side; ++y) {
        for (int x = -1; x <= Side; ++x) {
          above[static_cast<std::size_t>(y + 1)][static_cast<std::size_t>(x + 1)] =
              at(left + x, top + y) >= threshold;
        }
      }
      for (int y = 0; y < Side; ++y) {
        for (int x = 0; x < Side; ++x) {
          const bool side = above[static_cast<std::size_t>(y + 1)][static_cast<std::size_t>(x + 1)];
          bool uniform = true;
          for (int dy = 0; dy < 3 && uniform; ++dy) {
            for (int dx = 0; dx < 3 && uniform; ++dx) {
              uniform =
                  above[static_cast<std::size_t>(y + dy)][static_cast<std::size_t>(x + dx)] == side;
            }
          }
          if (!uniform) {
            continue;
          }
          const int px = left + x;
          const int py = top + y;
          const int sum = at(px - 1, py - 1) + (2 * at(px, py - 1)) + at(px + 1, py - 1) +
                          (2 * at(px - 1, py)) + (4 * at(px, py)) + (2 * at(px + 1, py)) +
                          at(px - 1, py + 1) + (2 * at(px, py + 1)) + at(px + 1, py + 1);
          const int centre = at(px, py);
          const int smoothed = std::clamp((sum + 8) >> 4, centre - limit, centre + limit);
          out[(static_cast<std::size_t>(py) * plane.width) + static_cast<std::size_t>(px)] =
              static_cast<std::int16_t>(smoothed);
        }
      }
    }
  }
}

template <typename Filter>
void eachPlane(PreciseFrame& frame, Filter filter) {
  const int qp = frame.quantizationScale * PreciseOne;
  filter(Plane{&frame.y, frame.width, frame.height}, qp);
  filter(Plane{&frame.cb, frame.width / 2, frame.height / 2}, qp);
  filter(Plane{&frame.cr, frame.width / 2, frame.height / 2}, qp);
}

} // namespace

void deblock(PreciseFrame& frame) {
  eachPlane(frame, deblockPlane);
}

void dering(PreciseFrame& frame) {
  eachPlane(frame, deringPlane);
}

} // namespace dwe::formats::video
