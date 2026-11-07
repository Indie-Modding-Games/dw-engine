#include "battle_glide.hpp"

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

} // namespace dwe::app
