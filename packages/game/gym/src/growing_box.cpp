#include <dwe/game/gym/growing_box.hpp>

namespace dwe::game::gym {
namespace {

constexpr std::int32_t LastFrame = GrowingBox::Frames - 1;

// renderUIBoxAnim: each corner moves a quarter of the way a frame, in whole pixels.
std::int32_t between(std::int32_t from, std::int32_t to, std::int32_t frame) {
  return from + (frame * (to - from) / LastFrame);
}

} // namespace

void GrowingBox::open(BoxRect from, BoxRect to) {
  from_ = from;
  to_ = to;
  frame_ = 0;
  drawn_.reset();
  state_ = State::Growing;
}

void GrowingBox::close(std::optional<BoxRect> to) {
  if (state_ == State::Closed || state_ == State::Shrinking) {
    return;
  }
  if (to) {
    from_ = *to;
  }
  state_ = State::Shrinking;
}

void GrowingBox::tickFrame() {
  // renderUIBoxAnimated: a growing box draws frame++ and stands once past the last; a shrinking
  // one draws --frame and is gone at 0.
  switch (state_) {
  case State::Growing:
    drawn_ = frame_++;
    if (frame_ > LastFrame) {
      state_ = State::Standing;
    }
    break;
  case State::Shrinking:
    drawn_ = --frame_;
    if (frame_ <= 0) {
      frame_ = 0;
      state_ = State::Closed;
    }
    break;
  case State::Standing:
  case State::Closed:
    drawn_.reset();
    break;
  }
}

std::optional<BoxRect> GrowingBox::outline() const {
  if (!drawn_) {
    return std::nullopt;
  }
  const std::int32_t frame = *drawn_;
  const std::int32_t left = between(from_.x, to_.x, frame);
  const std::int32_t top = between(from_.y, to_.y, frame);
  const std::int32_t right = between(from_.x + from_.w, to_.x + to_.w, frame);
  const std::int32_t bottom = between(from_.y + from_.h, to_.y + to_.h, frame);
  return BoxRect{.x = left, .y = top, .w = right - left, .h = bottom - top};
}

} // namespace dwe::game::gym
