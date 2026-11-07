#pragma once

#include <dwe/game/battle/battle.hpp>
#include <dwe/scene/components.hpp>

#include <optional>
#include <span>
#include <vector>

#include "scene_effect.hpp"

namespace dwe::app {

// Smooth motion in a battle (the smooth_motion setting, an engine improvement on by default): the
// battle moves its fighters and redraws its techniques' effects once per frame of the original, 20
// a second (battle.c: the fighters' and the effects' frame), every third game tick. Drawn where
// each frame leaves them, they step. These glide what is drawn from the last frame's place to the
// new one over the frame, one frame behind, as the field's characters glide between their frames
// (game::field::Field::drawnPose). Only the drawing changes: the battle's logic reads the frame's
// own places.

// How long a glide lasts: one frame of the battle.
inline constexpr double BattleFrameSeconds = 1.0 / game::battle::FramesPerSecond;

// A fighter's drawn place between two of the battle's frames.
class PoseGlide {
public:
  // A move longer than this in one frame is a jump (a fighter put somewhere else), drawn at once.
  static constexpr float JumpDistance = 500.0F;

  // The frame's place: the glide goes on to it from where it is drawn now; the first one, or a
  // jump, is drawn there at once.
  void moveTo(scene::Pose target);
  // The game's tick: the glide moves on by `seconds`.
  void advance(double seconds);
  // Where to draw it `ahead` seconds past the last tick (alpha times the tick).
  [[nodiscard]] scene::Pose drawn(double ahead) const;
  [[nodiscard]] bool started() const { return to_.has_value(); }

private:
  scene::Pose from_{};
  std::optional<scene::Pose> to_;
  double seconds_ = BattleFrameSeconds;
};

// The effects of the frame between `from` (the last frame's) and `to` (this one's), `progress`
// from 0 (the last frame) to 1 (this one): a sprite's point, a model object's translation and a
// streak's ends glide. Each of `to`'s draws pairs with the one in the same place in `from` among
// the draws of the same technique and kind; a technique whose draws of a kind changed in number
// (one appeared or went) is drawn as `to` has it, as is every other kind.
[[nodiscard]] std::vector<SceneEffect>
blendEffects(std::span<const SceneEffect> from, std::span<const SceneEffect> to, float progress);

} // namespace dwe::app
