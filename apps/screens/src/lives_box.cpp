#include <dwe/screens/lives_box.hpp>

namespace dwe::screens {

namespace {

// ui.c: renderUIBoxAnimated: the outline is drawn on box frames 0 to 4, a quarter of the way more
// each (renderUIBoxAnim); past the last the box is in place.
constexpr std::int32_t OutlineLastFrame = 4;
// murd.c: MURD_tickLivesBox: the life just lost shows full until frame 0xa, blinks until 0x1e
// (renderLivesBox: full on even pairs of frames, empty on odd ones), then shows empty.
constexpr std::int32_t BlinkFrom = 0xa;
constexpr std::int32_t EmptyFrom = 0x1e;
constexpr std::int32_t BlinkFramesPerShade = 2;

} // namespace

void LivesBox::open(WindowFraction anchor, std::int32_t lives, std::int32_t slots) {
  state_ = State::Opening;
  boxFrame_ = 0;
  livesFrame_ = 0;
  anchor_ = anchor;
  lives_ = lives;
  slots_ = slots;
  sound_ = BoxOpenSound;
}

void LivesBox::close(WindowFraction anchor) {
  if (state_ == State::Gone) {
    return;
  }
  // removeAnimatedUIBox: the outline shrinks from its last frame, drawn on the frame it starts
  // with the closing sound (renderUIBoxAnim).
  state_ = State::Closing;
  boxFrame_ = OutlineLastFrame;
  anchor_ = anchor;
  sound_ = BoxCloseSound;
}

void LivesBox::remove() {
  state_ = State::Gone;
  boxFrame_ = 0;
}

void LivesBox::step() {
  switch (state_) {
  case State::Opening:
    boxFrame_ += 1;
    if (boxFrame_ <= OutlineLastFrame) {
      break;
    }
    // renderUIBoxAnimated puts the box in place after drawing its last outline, so tickUIBox runs
    // MURD_tickLivesBox from the very next frame: the one the box first shows in place counts 1.
    state_ = State::Open;
    [[fallthrough]];
  case State::Open:
    livesFrame_ += 1;
    break;
  case State::Closing:
    boxFrame_ -= 1;
    if (boxFrame_ < 0) {
      remove();
    }
    break;
  case State::Gone:
    break;
  }
}

std::optional<std::int64_t> LivesBox::takeSound() {
  std::optional<std::int64_t> sound = sound_;
  sound_.reset();
  return sound;
}

ScreenSettings LivesBox::settings() const {
  const bool outline = state_ == State::Opening || state_ == State::Closing;
  const std::int32_t frame = outline ? boxFrame_ : OutlineLastFrame;
  const bool lostFull = livesFrame_ < BlinkFrom ||
                        (livesFrame_ < EmptyFrom && (livesFrame_ / BlinkFramesPerShade) % 2 == 0);
  return {
      {.name = std::string{LivesBoxShownSetting}, .value = shown()},
      {.name = std::string{LivesBoxOutlineSetting}, .value = outline},
      {.name = std::string{LivesBoxGrowSetting},
       .value = static_cast<double>(frame) / static_cast<double>(OutlineLastFrame)},
      {.name = std::string{LivesBoxAnchorXSetting}, .value = anchor_.x},
      {.name = std::string{LivesBoxAnchorYSetting}, .value = anchor_.y},
      {.name = std::string{LivesBoxLivesSetting}, .value = static_cast<double>(lives_)},
      {.name = std::string{LivesBoxSlotsSetting}, .value = static_cast<double>(slots_)},
      {.name = std::string{LivesBoxLostFullSetting}, .value = lostFull},
  };
}

} // namespace dwe::screens
