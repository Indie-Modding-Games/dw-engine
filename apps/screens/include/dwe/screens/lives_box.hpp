#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::screens {

// The settings of the lost life's lives box on its document (ui/lives_box/document), which redraws
// it on LivesBoxRevisionSetting: whether it shows, whether only its outline does (while it grows
// or shrinks), how far it has grown (0 at the partner, 1 in place), the partner's point it grows
// from in percent of the window, the lives left, the slots and whether the life just lost still
// shows full.
inline constexpr std::string_view LivesBoxRevisionSetting = "lives_box.revision";
inline constexpr std::string_view LivesBoxShownSetting = "lives_box.shown";
inline constexpr std::string_view LivesBoxOutlineSetting = "lives_box.outline";
inline constexpr std::string_view LivesBoxGrowSetting = "lives_box.grow";
inline constexpr std::string_view LivesBoxAnchorXSetting = "lives_box.anchor_x";
inline constexpr std::string_view LivesBoxAnchorYSetting = "lives_box.anchor_y";
inline constexpr std::string_view LivesBoxLivesSetting = "lives_box.lives";
inline constexpr std::string_view LivesBoxSlotsSetting = "lives_box.slots";
inline constexpr std::string_view LivesBoxLostFullSetting = "lives_box.lost_full";

// The sounds of the original's animated boxes, in slot 0 (ui.c: createAnimatedUIBox plays 0 as a
// box opens, renderUIBoxAnim 1 as it starts closing).
inline constexpr std::int64_t BoxOpenSound = 0;
inline constexpr std::int64_t BoxCloseSound = 1;

// The box a lost life shows over the partner (murd.c: MURD_createLivesBox, MURD_tickLivesBox,
// MURD_renderLivesBox, MURD_animateLivesBoxOut, MURD_removeLivesBox), on the original's animated
// box (ui.c: createAnimatedUIBox, renderUIBoxAnimated, removeAnimatedUIBox, removeStaticUIBox):
// its outline grows from the partner for 5 frames, then the box shows a slot per life, the one just
// lost full, blinking, then empty. With no life left it shrinks back to the partner; otherwise the
// scene takes it away at once. Frame by frame at the scene's rate.
class LivesBox {
public:
  // Opens the box over the partner's point with the lives left after the loss and a slot per life
  // the partner starts with.
  void open(WindowFraction anchor, std::int32_t lives, std::int32_t slots);
  // Shrinks the box back to the partner's point (MURD_animateLivesBoxOut).
  void close(WindowFraction anchor);
  // Takes the box away at once (MURD_removeLivesBox).
  void remove();
  // One frame of the scene.
  void step();

  // Whether the box shows, its outline included; false once it has shrunk away.
  [[nodiscard]] bool shown() const { return state_ != State::Gone; }
  [[nodiscard]] bool closing() const { return state_ == State::Closing; }
  // The sound the last open, close or step started, once.
  [[nodiscard]] std::optional<std::int64_t> takeSound();

  [[nodiscard]] ScreenSettings settings() const;

private:
  enum class State : std::uint8_t { Gone, Opening, Open, Closing };

  State state_ = State::Gone;
  // ui.c's box frame: up while the outline grows, down while it shrinks.
  std::int32_t boxFrame_ = 0;
  // MURD_tickLivesBox's frame, counted while the box is open.
  std::int32_t livesFrame_ = 0;
  WindowFraction anchor_;
  std::int32_t lives_ = 0;
  std::int32_t slots_ = 0;
  std::optional<std::int64_t> sound_;
};

} // namespace dwe::screens
