#pragma once

#include <dwe/animation/animation_player.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/hit_particles.hpp>
#include <dwe/scene/components.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
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

// A held fighter's clip between two ticks. The battle's clips run on by whole ticks
// (FieldScene::runFighters), 60 a second, so at a higher display rate the fighters' bodies step
// from pose to pose. This draws the clip at the time between the last two ticks', one tick behind
// as Field::drawnPose. Only the clip's time is mixed, inside the clip it plays now: a new clip, or
// the same one started over, is drawn from its own start, never blended with the last one. The
// player keeps the tick's time and pose, which the battle reads (FieldScene::fighterPose,
// fighterPlayedThrough).
class ClipTween {
public:
  // After a drawn frame moved `player` on: `ticked` when ticks ran this frame (the clip moved
  // `tickStep` of its time a tick, or stood still when frozen).
  void follow(const animation::AnimationPlayer& player, bool ticked, double tickStep);
  // The clip's time `alpha` of a tick past the last one: from the last tick's to this one's.
  [[nodiscard]] double timeAt(float alpha) const;
  // Samples the pose at timeAt(alpha) into the tween's own pose and joint matrices.
  void pose(const assets::ModelData& model, float alpha);
  // Whether pose() has something to draw.
  [[nodiscard]] bool posed() const { return clip_ != nullptr && !joints_.empty(); }
  [[nodiscard]] std::span<const assets::NodeTransform> nodes() const { return pose_; }
  [[nodiscard]] std::span<const glm::mat4> joints() const { return joints_; }

private:
  const assets::AnimationClip* clip_ = nullptr;
  double from_ = 0.0;
  double to_ = 0.0;
  std::vector<assets::NodeTransform> pose_;
  std::vector<glm::mat4> nodeWorld_;
  std::vector<glm::mat4> joints_;
};

// The hit sparks and the status visuals change once a battle frame. Smooth motion draws them
// between the last frame's and this one's, `progress` from 0 (the last frame) to 1 (this one), one
// frame behind as the effects (blendEffects). Each thing pairs with the same one on the last frame
// (same slot, same owner, one frame younger); one that just appeared, ended or jumped is drawn as
// this frame has it, so nothing smears.

// A count that moves `step` a frame (an effect's frame or life), drawn between the last frame's
// `from` and this one's `to`: on the way from one to the other when it moved by exactly `step`;
// else (it started, ended or jumped) `to`. At progress 1, `to`.
[[nodiscard]] float betweenFrames(std::int32_t from, std::int32_t to, float progress,
                                  std::int32_t step = 1);
// A value with no step of its own (a place, a size) on the way from `from` to `to`.
[[nodiscard]] float between(float from, float to, float progress);
// An angle of `turn` units a whole turn on the way from `from` to `to`, the short way round.
[[nodiscard]] float betweenAngles(float from, float to, float turn, float progress);

// The slot's thing of the last frame when `same` says it is this frame's, one frame younger.
template <typename T, std::size_t N, typename Same>
[[nodiscard]] const T* lastFramesOf(const std::array<std::optional<T>, N>* last, std::size_t slot,
                                    const T& now, Same same) {
  if (last == nullptr || slot >= N || !(*last)[slot] || !same(*(*last)[slot], now)) {
    return nullptr;
  }
  return &*(*last)[slot];
}

// How far behind its frame, 0 to 1 of a frame, smooth motion draws a hit spark of slot `slot`
// (the same spark one frame younger in `last`) or a cloud (one at the same spot one frame
// younger); 0 for a new one or without the last frame.
[[nodiscard]] float sparkBehind(const game::battle::HitParticles* last, std::size_t slot,
                                const game::battle::HitParticles::SparkFx& now, float progress);
[[nodiscard]] float cloudBehind(const game::battle::HitParticles* last,
                                const game::battle::HitParticles::Cloud& now, float progress);

} // namespace dwe::app
