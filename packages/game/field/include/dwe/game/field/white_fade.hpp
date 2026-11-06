#pragma once

#include <dwe/game/field/rules.hpp>

#include <cstdint>
#include <optional>

namespace dwe::game::field {

// The story's fade to white and back (story routines 45 and 46: fade.c: fadeToWhite and
// fadeFromWhite, drawn by renderFadeOut and renderFadeIn in FADE_MODE 1): the same grey "shade" as
// the map change's fade, added to the screen instead of subtracted. Counted in frames of the
// original, as its fade steps once per frame.
//
// The fade out goes up from the shade the last fade left (FADE_PROGRESS) by shadeMax / (frames + 1)
// a frame and holds shadeMax until the fade in or a map change takes it away. The fade in starts
// one step under shadeMax, draws `frames` frames and then stops on the frame after, wherever it got
// to (40 of 160 for 40 frames: the screen jumps from a light white to none).
class WhiteFade {
public:
  explicit WhiteFade(TransitionRules rules = {}) : rules_{rules} {}

  // fadeToWhite(frames): from `from`, the shade FADE_PROGRESS holds, the first step drawn at once
  // (the routine runs in the frame's tick, before its render).
  void fadeOut(std::int32_t frames, std::int32_t from);
  // fadeFromWhite(frames): replaces a fade out, its first step drawn at once.
  void fadeIn(std::int32_t frames);
  // A map change's fadeToBlack removes the fade (fade.c: removeObject(4005)); FADE_PROGRESS keeps
  // what it drew.
  void stop() { mode_ = Mode::None; }
  // One more frame of the original.
  void frame();

  // Whether the fade draws this frame.
  [[nodiscard]] bool active() const { return mode_ != Mode::None; }
  // The shade drawn this frame, or the one the fade left when it stopped (FADE_PROGRESS).
  [[nodiscard]] std::int32_t shade() const { return progress_; }
  // How much white this frame adds to the screen, from 0 to 1: the fade's texel is white and the
  // console adds it times shade / 128 (FADE_MODE 1, B + F), saturating; 0 when it does not draw.
  [[nodiscard]] float whiteness() const;

private:
  enum class Mode : std::uint8_t { None, Out, In };

  void drawOut();
  void drawIn();

  TransitionRules rules_;
  Mode mode_ = Mode::None;
  // shadeMax / (frames + 1) (FADE_DATA, FADE_IN_TARGET).
  std::int32_t step_ = 0;
  // The fade in's frames and how many it drew (FADE_IN_CURRENT).
  std::int32_t frames_ = 0;
  std::int32_t drawn_ = 0;
  // FADE_PROGRESS.
  std::int32_t progress_ = 0;
};

} // namespace dwe::game::field
