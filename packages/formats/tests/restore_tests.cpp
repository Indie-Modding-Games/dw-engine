#include <dwe/formats/video/restore.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace dwe::formats::video;

namespace {

// A 32x16 frame (two macroblocks side by side) whose luma is level(x, y) in sixteenths; chroma
// grey.
template <typename Level>
PreciseFrame frameOf(std::uint16_t scale, Level level) {
  PreciseFrame frame{.width = 32,
                     .height = 16,
                     .quantizationScale = scale,
                     .y = std::vector<std::int16_t>(std::size_t{32} * 16),
                     .cb = std::vector<std::int16_t>(std::size_t{16} * 8, 128 * PreciseOne),
                     .cr = std::vector<std::int16_t>(std::size_t{16} * 8, 128 * PreciseOne)};
  for (std::size_t y = 0; y < 16; ++y) {
    for (std::size_t x = 0; x < 32; ++x) {
      frame.y[(y * 32) + x] = static_cast<std::int16_t>(level(x, y));
    }
  }
  return frame;
}

int lumaAt(const PreciseFrame& frame, std::size_t x, std::size_t y) {
  return frame.y[(y * frame.width) + x];
}

} // namespace

TEST_CASE("deblocking evens out a small step between flat blocks") {
  // Blocks of 100 and 103 levels side by side: a step the scale (4) explains.
  auto frame = frameOf(
      4, [](std::size_t x, std::size_t /*y*/) { return (x < 16 ? 100 : 103) * PreciseOne; });
  deblock(frame);
  // The edge between x = 15 and 16 becomes a ramp: no step left larger than a level.
  for (std::size_t x = 11; x < 21; ++x) {
    CAPTURE(x);
    CHECK(std::abs(lumaAt(frame, x + 1, 5) - lumaAt(frame, x, 5)) <= PreciseOne);
  }
  CHECK(lumaAt(frame, 15, 5) > 100 * PreciseOne);
  CHECK(lumaAt(frame, 16, 5) < 103 * PreciseOne);
  // Far from the edge nothing moves.
  CHECK(lumaAt(frame, 2, 5) == 100 * PreciseOne);
  CHECK(lumaAt(frame, 30, 5) == 103 * PreciseOne);
}

TEST_CASE("deblocking keeps a real edge") {
  // A 60-level step is an edge of the picture, far beyond what a scale of 4 leaves behind.
  const auto source =
      frameOf(4, [](std::size_t x, std::size_t /*y*/) { return (x < 16 ? 60 : 120) * PreciseOne; });
  auto frame = source;
  deblock(frame);
  CHECK(frame.y == source.y);
}

TEST_CASE("a stronger scale smooths a larger step") {
  const auto step = [](std::size_t x, std::size_t /*y*/) {
    return (x < 16 ? 100 : 112) * PreciseOne;
  };
  auto fine = frameOf(2, step);
  auto coarse = frameOf(12, step);
  deblock(fine);
  deblock(coarse);
  CHECK(lumaAt(fine, 15, 3) == 100 * PreciseOne);
  CHECK(lumaAt(coarse, 15, 3) > 100 * PreciseOne);
}

TEST_CASE("deringing smooths ripples beside an edge by no more than half the scale") {
  // A dark-to-bright edge inside the block, with a ripple of +-3 levels on the bright side.
  const auto source = frameOf(8, [](std::size_t x, std::size_t y) {
    if (x < 4) {
      return 40 * PreciseOne;
    }
    return (200 + ((x + y) % 2 == 0 ? 3 : -3)) * PreciseOne;
  });
  auto frame = source;
  dering(frame);
  bool smoothed = false;
  for (std::size_t i = 0; i < frame.y.size(); ++i) {
    CHECK(std::abs(frame.y[i] - source.y[i]) <= 8 * PreciseOne / 2);
    smoothed = smoothed || frame.y[i] != source.y[i];
  }
  CHECK(smoothed);
  // The edge itself, where the neighbours straddle the mid level, stays as it was.
  CHECK(lumaAt(frame, 3, 4) == 40 * PreciseOne);
  CHECK(lumaAt(frame, 4, 4) == source.y[(4 * 32) + 4]);
  // A flat block is left alone.
  auto flat = frameOf(8, [](std::size_t /*x*/, std::size_t /*y*/) { return 90 * PreciseOne; });
  dering(flat);
  CHECK(std::ranges::all_of(flat.y, [](std::int16_t sample) { return sample == 90 * PreciseOne; }));
}

TEST_CASE("the clean-up is the same every time") {
  const auto level = [](std::size_t x, std::size_t y) {
    return static_cast<int>(((x * 37) + (y * 91)) % 200) * PreciseOne;
  };
  auto first = frameOf(6, level);
  auto second = frameOf(6, level);
  for (auto* frame : {&first, &second}) {
    deblock(*frame);
    dering(*frame);
  }
  CHECK(first.y == second.y);
  CHECK(first.cb == second.cb);
}
