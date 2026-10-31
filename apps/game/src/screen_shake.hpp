#pragma once

#include <algorithm>
#include <cstdint>

namespace dwe::app {

// The lava cave's shake (story routine 34: map.c: createMeramonShake, tickMeramonShake): for 210
// frames the drawing moves sideways, away on the even frames and back on the odd ones, and a square
// of the glow image grows from the screen's centre by 32 pixels to each side every even frame up
// to 200, drawn subtractive in cyan, so the screen goes red.
//
// The square shakes with the drawing (it is placed against the drawing offset). On its last frame
// the object puts the offset back and removes itself, but the square it sorted that frame is still
// drawn: it shows until the next frame.
//
// Measured in the original (tunn08, the scene with trigger 230 clear): after 7 frames the square's
// half side is 128 (MERAMON_SHAKE_POS_X -128), after 16 the whole screen is red (green and blue
// taken away, red kept), and the dialogue box neither shakes nor reddens.
class ScreenShake {
public:
  // tickMeramonShake removes itself once MERAMON_SHAKE_FRAME_COUNT reaches 0xd2.
  static constexpr std::int32_t Frames = 0xD2;
  // MERAMON_SHAKE_OFFSET (0x20): how much the square grows to each side on an even frame.
  static constexpr std::int32_t SquareStep = 0x20;
  // MERAMON_SHAKE_POS_X and _Y stop at -200.
  static constexpr std::int32_t SquareLimit = 200;

  // createMeramonShake: from frame 0, with no square.
  void start() {
    frame_ = 0;
    offset_ = 0;
    squareHalf_ = 0;
    running_ = true;
    squareShown_ = false;
  }

  // One frame of the original.
  void frame() {
    if (!running_) {
      squareShown_ = false;
      return;
    }
    std::int32_t step = 1;
    if (frame_ >= 0xB5) {
      step = 1;
    } else if (frame_ >= 0x79) {
      step = 2;
    } else if (frame_ >= 0x3D) {
      step = 3;
    } else if (frame_ >= 0x1F) {
      step = 2;
    }
    if (frame_ % 2 == 0) {
      step = -step;
      squareHalf_ = std::min(squareHalf_ + SquareStep, SquareLimit);
    }
    offset_ += step;
    squareShown_ = true;
    ++frame_;
    if (frame_ >= Frames) {
      // The drawing offset goes back to where it was when the shake started.
      offset_ = 0;
      running_ = false;
    }
  }

  // Whether a frame still changes something: the shake goes on or its last square is drawn.
  [[nodiscard]] bool busy() const { return running_ || squareShown_; }
  [[nodiscard]] bool running() const { return running_; }
  // The frames done (MERAMON_SHAKE_FRAME_COUNT).
  [[nodiscard]] std::int32_t frames() const { return frame_; }
  // How far the drawing is moved to the right, in original pixels.
  [[nodiscard]] std::int32_t offset() const { return offset_; }
  // Whether the square is drawn now, and its half side in original pixels (-MERAMON_SHAKE_POS_X).
  [[nodiscard]] bool squareShown() const { return squareShown_; }
  [[nodiscard]] std::int32_t squareHalf() const { return squareHalf_; }

private:
  std::int32_t frame_ = 0;
  std::int32_t offset_ = 0;
  std::int32_t squareHalf_ = 0;
  bool running_ = false;
  bool squareShown_ = false;
};

} // namespace dwe::app
