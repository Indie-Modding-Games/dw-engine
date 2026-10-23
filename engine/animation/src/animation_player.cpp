#include <dwe/animation/animation_player.hpp>
#include <dwe/animation/clip_sampling.hpp>

#include <algorithm>
#include <functional>
#include <span>

namespace dwe::animation {

AnimationPlayer::AnimationPlayer(const assets::ModelData& model)
    : model_(&model), sampled_(model.nodes.size()), pose_(model.nodes.size()),
      nodeWorld_(model.nodes.size()), joints_(jointCount(model)) {
  restPose(model, pose_);
  computeJointMatrices(model, pose_, nodeWorld_, joints_);
}

bool AnimationPlayer::play(const assets::AnimationClip& clip, float fadeSeconds, float speed) {
  // Pointers into one array compare by their position in it; std::less orders any two pointers.
  const std::span<const assets::AnimationClip> own = model_->clips;
  if (own.empty() || std::less<>{}(&clip, own.data()) ||
      !std::less<>{}(&clip, own.data() + own.size())) {
    return false;
  }
  clip_ = &clip;
  time_ = 0.0;
  speed_ = speed;
  fadeSeconds_ = std::max(fadeSeconds, 0.0F);
  fadeTime_ = 0.0F;
  if (fadeSeconds_ > 0.0F) {
    fadeFrom_ = pose_;
  }
  update();
  return true;
}

bool AnimationPlayer::play(std::uint32_t clipId, float fadeSeconds, float speed) {
  const assets::AnimationClip* clip = model_->findClip(clipId);
  if (clip == nullptr) {
    return false;
  }
  return play(*clip, fadeSeconds, speed);
}

glm::vec3 AnimationPlayer::advance(float seconds) {
  if (clip_ == nullptr) {
    return glm::vec3{0.0F};
  }
  const double step = static_cast<double>(std::max(seconds, 0.0F)) * static_cast<double>(speed_);
  const glm::vec3 before = rootMotion(*model_, *clip_, time_);
  time_ += step;
  fadeTime_ += std::max(seconds, 0.0F);
  update();
  return rootMotion(*model_, *clip_, time_) - before;
}

bool AnimationPlayer::finished() const {
  return clip_ != nullptr && !clip_->repeat && time_ >= static_cast<double>(clip_->duration);
}

bool AnimationPlayer::playedThrough() const {
  return clip_ != nullptr && time_ >= static_cast<double>(clip_->duration);
}

void AnimationPlayer::update() {
  if (clip_ == nullptr) {
    return;
  }
  if (fadeSeconds_ > 0.0F && fadeTime_ < fadeSeconds_) {
    sampleClip(*model_, *clip_, time_, sampled_);
    blendPoses(fadeFrom_, sampled_, fadeTime_ / fadeSeconds_, pose_);
  } else {
    sampleClip(*model_, *clip_, time_, pose_);
  }
  computeJointMatrices(*model_, pose_, nodeWorld_, joints_);
}

} // namespace dwe::animation
