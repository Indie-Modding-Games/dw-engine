#include <dwe/screens/command_menu.hpp>

#include <array>
#include <cstddef>
#include <string>

namespace dwe::screens {
namespace {

// command_menu.c: BTL_COMMAND_MENU_SLIDE_Y and BTL_COMMAND_MENU_SLIDE_X, the strip's bottom and top
// edge at each step.
constexpr std::array<std::int32_t, 8> SlideBottom{-120, -106, -94, -84, -77, -72, -69, -68};
constexpr std::array<std::int32_t, 8> SlideTop{-152, -151, -148, -143, -136, -126, -114, -100};
constexpr std::int32_t LastStep = 7;
// std_main.c: STD_addCommandMenu: STD_COMMAND_MENU_TOP 0x44 and the bottom 0x20 under it.
constexpr std::int32_t MatchTop = 0x44;
constexpr std::int32_t MatchBottom = MatchTop + 0x20;
constexpr std::uint8_t BlinkFrames = 8;
// BTL_isCommandMenuClosed answers true when its count reaches 4.
constexpr std::int32_t ClosedAsks = 4;

} // namespace

void CommandMenuMotion::openForMatch() {
  open();
  match_ = true;
}

void CommandMenuMotion::open() {
  state_ = State::Open;
  match_ = false;
  bottomStep_ = 0;
  topStep_ = 0;
  timer_ = 0;
  blink_ = false;
  closedFrames_ = 0;
}

void CommandMenuMotion::close() {
  if (state_ == State::Open) {
    state_ = State::Closing;
  }
}

void CommandMenuMotion::step(bool fighting) {
  if (state_ == State::Gone) {
    return;
  }
  ++timer_;
  if (state_ == State::Open) {
    if (match_) {
      if (fighting && timer_ % BlinkFrames == 0) {
        blink_ = !blink_;
      }
      return;
    }
    if (bottomStep_ < LastStep) {
      ++bottomStep_;
    }
    if (bottomStep_ == LastStep && topStep_ < LastStep) {
      ++topStep_;
    }
    if (fighting && timer_ % BlinkFrames == 0) {
      blink_ = !blink_;
    }
    return;
  }
  if (topStep_ != 0) {
    --topStep_;
  }
  if (topStep_ == 0 && bottomStep_ != 0) {
    --bottomStep_;
  }
}

bool CommandMenuMotion::closed() {
  if (match_ && state_ != State::Open) {
    state_ = State::Gone;
    return true;
  }
  if (state_ == State::Open || topStep_ != 0 || bottomStep_ != 0) {
    return false;
  }
  // BTL_removeCommandMenu: the strip is gone from the first ask on.
  state_ = State::Gone;
  ++closedFrames_;
  return closedFrames_ >= ClosedAsks;
}

std::int32_t CommandMenuMotion::top() const {
  if (match_ && state_ != State::Gone) {
    return MatchTop;
  }
  return SlideTop.at(static_cast<std::size_t>(topStep_));
}

std::int32_t CommandMenuMotion::bottom() const {
  if (match_ && state_ != State::Gone) {
    return MatchBottom;
  }
  return SlideBottom.at(static_cast<std::size_t>(bottomStep_));
}

ScreenSettings CommandMenuMotion::settings() const {
  return {
      {.name = std::string{CommandMenuTopSetting}, .value = static_cast<double>(top())},
      {.name = std::string{CommandMenuBottomSetting}, .value = static_cast<double>(bottom())},
      {.name = std::string{CommandMenuBlinkSetting}, .value = blink_},
  };
}

} // namespace dwe::screens
