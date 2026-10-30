#pragma once

#include <cstdint>
#include <optional>

namespace dwe::game::gym {

// A box's rectangle in the original's pixels, counted from the middle of its 320 x 240 screen.
struct BoxRect {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t w = 0;
  std::int32_t h = 0;

  friend bool operator==(const BoxRect&, const BoxRect&) = default;
};

// ui.c: createAnimatedUIBox, renderUIBoxAnimated and removeAnimatedUIBox: a box opens as a grey
// outline growing from a small rectangle to its own in five frames (0 to 4), stands, and closes the
// same way back. The box's tick and contents run only while it stands.
class GrowingBox {
public:
  // The frames of the outline each way.
  static constexpr std::int32_t Frames = 5;

  // createAnimatedUIBox: grows from `from` to `to`.
  void open(BoxRect from, BoxRect to);
  // removeAnimatedUIBox: shrinks back to where it grew from, or to `to`.
  void close(std::optional<BoxRect> to = std::nullopt);

  // One drawn frame of the original (renderUIBoxAnimated).
  void tickFrame();

  // The outline this frame, while it grows or shrinks (renderUIBoxAnim).
  [[nodiscard]] std::optional<BoxRect> outline() const;
  // Whether the box stands, drawn with its contents.
  [[nodiscard]] bool standing() const { return state_ == State::Standing; }
  // Whether nothing of it is drawn any more.
  [[nodiscard]] bool closed() const { return state_ == State::Closed; }
  // Whether it is growing, standing or about to (not closing or closed).
  [[nodiscard]] bool opened() const { return state_ == State::Growing || standing(); }

private:
  enum class State : std::uint8_t { Closed, Growing, Standing, Shrinking };

  State state_ = State::Closed;
  BoxRect from_;
  BoxRect to_;
  // UIBoxData.frame, and the outline's frame drawn last.
  std::int32_t frame_ = 0;
  std::optional<std::int32_t> drawn_;
};

} // namespace dwe::game::gym
