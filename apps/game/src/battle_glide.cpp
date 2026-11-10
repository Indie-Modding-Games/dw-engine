#include "battle_glide.hpp"

#include <dwe/animation/clip_sampling.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <numbers>
#include <string>
#include <utility>
#include <variant>

namespace dwe::app {

namespace {

constexpr float Turn = 2.0F * std::numbers::pi_v<float>;

std::int32_t mix(std::int32_t from, std::int32_t to, float progress) {
  return from + static_cast<std::int32_t>(std::lround(static_cast<float>(to - from) * progress));
}

game::battle::EffectVector mix(const game::battle::EffectVector& from,
                               const game::battle::EffectVector& to, float progress) {
  return {.x = mix(from.x, to.x, progress),
          .y = mix(from.y, to.y, progress),
          .z = mix(from.z, to.z, progress)};
}

// The draw's place glides from the paired one's; kinds with no place keep `to`'s.
void glide(game::battle::EffectShape& to, const game::battle::EffectShape& from, float progress) {
  if (auto* sprite = std::get_if<game::battle::EffectSprite>(&to)) {
    sprite->position =
        mix(std::get<game::battle::EffectSprite>(from).position, sprite->position, progress);
  } else if (auto* model = std::get_if<game::battle::EffectModel>(&to)) {
    model->transform.t =
        mix(std::get<game::battle::EffectModel>(from).transform.t, model->transform.t, progress);
  } else if (auto* streak = std::get_if<game::battle::EffectStreak>(&to)) {
    const auto& before = std::get<game::battle::EffectStreak>(from);
    streak->from = mix(before.from, streak->from, progress);
    streak->to = mix(before.to, streak->to, progress);
  }
}

using GroupKey = std::pair<std::string, std::size_t>;

GroupKey groupOf(const SceneEffect& effect) {
  return {effect.move, effect.shape.index()};
}

} // namespace

void PoseGlide::moveTo(scene::Pose target) {
  if (to_ && target.position == to_->position && target.yaw == to_->yaw) {
    return;
  }
  if (!to_ || glm::distance(to_->position, target.position) > JumpDistance) {
    from_ = target;
  } else {
    from_ = drawn(0.0);
  }
  to_ = target;
  seconds_ = 0.0;
}

void PoseGlide::advance(double seconds) {
  seconds_ = std::min(seconds_ + seconds, BattleFrameSeconds);
}

scene::Pose PoseGlide::drawn(double ahead) const {
  if (!to_) {
    return from_;
  }
  const auto progress =
      static_cast<float>(std::clamp((seconds_ + ahead) / BattleFrameSeconds, 0.0, 1.0));
  const float turn = std::remainder(to_->yaw - from_.yaw, Turn);
  return {.position = glm::mix(from_.position, to_->position, progress),
          .yaw = from_.yaw + (turn * progress)};
}

std::vector<SceneEffect> blendEffects(std::span<const SceneEffect> from,
                                      std::span<const SceneEffect> to, float progress) {
  std::vector<SceneEffect> out{to.begin(), to.end()};
  if (progress >= 1.0F || from.empty()) {
    return out;
  }
  // Each technique's draws of each kind, in order, in both frames.
  std::map<GroupKey, std::vector<std::size_t>> before;
  for (std::size_t i = 0; i < from.size(); ++i) {
    before[groupOf(from[i])].push_back(i);
  }
  std::map<GroupKey, std::vector<std::size_t>> now;
  for (std::size_t i = 0; i < to.size(); ++i) {
    now[groupOf(to[i])].push_back(i);
  }
  for (const auto& [key, indices] : now) {
    const auto found = before.find(key);
    if (found == before.end() || found->second.size() != indices.size()) {
      continue;
    }
    for (std::size_t k = 0; k < indices.size(); ++k) {
      glide(out[indices[k]].shape, from[found->second[k]].shape, progress);
    }
  }
  return out;
}

void ClipTween::follow(const animation::AnimationPlayer& player, bool ticked, double tickStep) {
  const assets::AnimationClip* clip = player.clip();
  const double now = player.time();
  // Another clip, or this one started over: drawn from where it is, with nothing of the last.
  if (clip != clip_ || now < to_) {
    clip_ = clip;
    from_ = now;
    to_ = now;
    return;
  }
  if (ticked) {
    // Several ticks in one drawn frame: only the last one is drawn on the way.
    from_ = std::max(to_, now - tickStep);
    to_ = now;
  }
}

double ClipTween::timeAt(float alpha) const {
  return from_ + ((to_ - from_) * static_cast<double>(std::clamp(alpha, 0.0F, 1.0F)));
}

void ClipTween::pose(const assets::ModelData& model, float alpha) {
  if (clip_ == nullptr) {
    return;
  }
  pose_.resize(model.nodes.size());
  nodeWorld_.resize(model.nodes.size());
  joints_.resize(animation::jointCount(model));
  animation::sampleClip(model, *clip_, timeAt(alpha), pose_);
  animation::computeJointMatrices(model, pose_, nodeWorld_, joints_);
}

float betweenFrames(std::int32_t from, std::int32_t to, float progress, std::int32_t step) {
  if (progress >= 1.0F || to - from != step) {
    return static_cast<float>(to);
  }
  return static_cast<float>(from) + (static_cast<float>(step) * std::max(progress, 0.0F));
}

float between(float from, float to, float progress) {
  if (progress >= 1.0F) {
    return to;
  }
  return from + ((to - from) * std::max(progress, 0.0F));
}

float betweenAngles(float from, float to, float turn, float progress) {
  if (progress >= 1.0F) {
    return to;
  }
  return to - (std::remainder(to - from, turn) * (1.0F - std::max(progress, 0.0F)));
}

float sparkBehind(const game::battle::HitParticles* last, std::size_t slot,
                  const game::battle::HitParticles::SparkFx& now, float progress) {
  if (last == nullptr || progress >= 1.0F) {
    return 0.0F;
  }
  const auto slots = last->sparkSlots();
  if (slot >= slots.size() || !slots[slot]) {
    return 0.0F;
  }
  const auto& before = *slots[slot];
  const bool same = before.spark == now.spark && before.kind == now.kind &&
                    before.position == now.position && before.fighter == now.fighter &&
                    before.spread == now.spread && before.state + 1 == now.state;
  return same ? 1.0F - std::max(progress, 0.0F) : 0.0F;
}

float cloudBehind(const game::battle::HitParticles* last,
                  const game::battle::HitParticles::Cloud& now, float progress) {
  if (last == nullptr || progress >= 1.0F) {
    return 0.0F;
  }
  const bool found = std::ranges::any_of(last->clouds(), [&now](const auto& before) {
    return before.x == now.x && before.z == now.z && before.state + 1 == now.state;
  });
  return found ? 1.0F - std::max(progress, 0.0F) : 0.0F;
}

} // namespace dwe::app
