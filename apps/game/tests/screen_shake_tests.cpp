#include <doctest/doctest.h>

#include "screen_shake.hpp"

using dwe::app::ScreenShake;

TEST_CASE("the shake's square grows 32 a side every even frame up to 200, as in the original") {
  ScreenShake shake;
  CHECK_FALSE(shake.busy());
  shake.start();
  CHECK_FALSE(shake.squareShown());
  shake.frame();
  CHECK(shake.squareShown());
  CHECK(shake.squareHalf() == 32);
  shake.frame();
  CHECK(shake.squareHalf() == 32);
  // Measured: MERAMON_SHAKE_POS_X is -128 once 7 frames are done.
  for (int frame = 2; frame < 7; ++frame) {
    shake.frame();
  }
  CHECK(shake.frames() == 7);
  CHECK(shake.squareHalf() == 128);
  for (int frame = 7; frame < 40; ++frame) {
    shake.frame();
  }
  CHECK(shake.squareHalf() == ScreenShake::SquareLimit);
}

TEST_CASE("the shake moves the drawing away on even frames and back on odd ones") {
  ScreenShake shake;
  shake.start();
  shake.frame();
  CHECK(shake.offset() == -1);
  shake.frame();
  CHECK(shake.offset() == 0);
  // Frame 30 steps 1 away, frame 31 steps 2 back: the picture rests a pixel to the right.
  for (int frame = 2; frame < 32; ++frame) {
    shake.frame();
  }
  CHECK(shake.offset() == 1);
}

TEST_CASE("the shake ends on its 210th frame, its last square drawn until the next frame") {
  ScreenShake shake;
  shake.start();
  for (int frame = 0; frame < ScreenShake::Frames; ++frame) {
    shake.frame();
  }
  CHECK_FALSE(shake.running());
  CHECK(shake.offset() == 0);
  CHECK(shake.squareShown());
  CHECK(shake.busy());
  shake.frame();
  CHECK_FALSE(shake.squareShown());
  CHECK_FALSE(shake.busy());
}
