// Holding confirm keeps moving items, slow at first and quicker as the hold goes on
// (apps/game/src/confirm_repeat.hpp).

#include <doctest/doctest.h>

#include <vector>

#include "confirm_repeat.hpp"

namespace dwe::app {
namespace {

constexpr double Tick = 1.0 / 60.0;

// Runs `ticks` held ticks after the press and returns the moves each made.
std::vector<int> hold(ConfirmRepeat& repeat, int ticks) {
  std::vector<int> out;
  for (int i = 0; i < ticks; ++i) {
    out.push_back(repeat.tick(true, false, Tick));
  }
  return out;
}

} // namespace

TEST_CASE("a press moves one and its first held move comes after a slow wait") {
  ConfirmRepeat repeat;
  CHECK(repeat.tick(true, true, Tick) == 1);
  // The 0.45 s delay before the first held move is about 27 ticks; none of them moves.
  for (int i = 0; i < 26; ++i) {
    CHECK(repeat.tick(true, false, Tick) == 0);
  }
}

TEST_CASE("a held confirm keeps moving items") {
  ConfirmRepeat repeat;
  repeat.tick(true, true, Tick);
  int moved = 0;
  for (const int step : hold(repeat, 600)) {
    moved += step;
  }
  CHECK(moved > 10);
}

TEST_CASE("the hold speeds up: later gaps hold fewer ticks than earlier ones") {
  ConfirmRepeat repeat;
  repeat.tick(true, true, Tick);
  // Read the gaps between the first held moves and the last ones.
  std::vector<int> firstGaps;
  std::vector<int> lastGaps;
  int waiting = 0;
  int moves = 0;
  for (int i = 0; i < 240; ++i) {
    ++waiting;
    const int made = repeat.tick(true, false, Tick);
    if (made > 0) {
      if (moves < 2) {
        firstGaps.push_back(waiting);
      }
      lastGaps.push_back(waiting);
      waiting = 0;
      moves += made;
    }
  }
  CHECK_FALSE(firstGaps.empty());
  CHECK_FALSE(lastGaps.empty());
  CHECK(lastGaps.back() < firstGaps.front());
}

TEST_CASE("releasing stops the hold and a fresh tap moves one again") {
  ConfirmRepeat repeat;
  repeat.tick(true, true, Tick);
  CHECK(repeat.tick(false, false, Tick) == 0);
  CHECK(repeat.tick(true, false, Tick) == 0);
  CHECK(repeat.tick(true, true, Tick) == 1);
}

TEST_CASE("a tap that is up again within its tick still moves one") {
  ConfirmRepeat repeat;
  CHECK(repeat.tick(false, true, Tick) == 1);
  CHECK(repeat.tick(false, false, Tick) == 0);
}

TEST_CASE("a confirm held from before the windows opened does not start the hold") {
  ConfirmRepeat repeat;
  // The press that opened the windows was spent in an earlier phase.
  CHECK(repeat.tick(true, false, Tick) == 0);
}

TEST_CASE("stop drops the hold so the next press starts slow again") {
  ConfirmRepeat repeat;
  repeat.tick(true, true, Tick);
  hold(repeat, 60);
  repeat.stop();
  CHECK(repeat.tick(true, false, Tick) == 0);
  CHECK(repeat.tick(true, true, Tick) == 1);
}

} // namespace dwe::app
