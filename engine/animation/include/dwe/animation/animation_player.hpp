#pragma once

#include <dwe/assets/model_data.hpp>
#include <dwe/core/math.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace dwe::animation {

// Plays the clips of one model instance: keeps its time, samples the pose, separates the root motion and computes the
// joint matrices that render skins with. The model must outlive the player.
class AnimationPlayer {
public:
    // Starts at the rest pose, with no clip.
    explicit AnimationPlayer(const assets::ModelData& model);

    // Starts a clip from its beginning. With a fade, the pose blends from the current one over that many seconds.
    // `speed` scales time (1 = as the original; use it to match a walk cycle to a ground speed). The clip must be one
    // of the player's model: another model's is refused (false) and the player keeps what it was playing.
    bool play(const assets::AnimationClip& clip, float fadeSeconds = 0.0F, float speed = 1.0F);
    // Starts the model's clip with this original id; false if the model has none.
    bool play(std::uint32_t clipId, float fadeSeconds = 0.0F, float speed = 1.0F);
    void setSpeed(float speed) { speed_ = speed; }

    // Moves time on and updates the pose and the joint matrices. Returns the root motion of this step, in the model's
    // space as if the entity faced its rest direction: the field decides whether to move the entity by it.
    glm::vec3 advance(float seconds);

    [[nodiscard]] const assets::AnimationClip* clip() const { return clip_; }
    // Seconds of clip time since play().
    [[nodiscard]] double time() const { return time_; }
    // A clip that does not repeat is finished once it holds its last pose.
    [[nodiscard]] bool finished() const;
    [[nodiscard]] std::span<const assets::NodeTransform> pose() const { return pose_; }
    // One per joint of the skin (see computeJointMatrices).
    [[nodiscard]] std::span<const glm::mat4> jointMatrices() const { return joints_; }

private:
    void update();

    const assets::ModelData* model_;
    const assets::AnimationClip* clip_ = nullptr;
    double time_ = 0.0;
    float speed_ = 1.0F;
    float fadeSeconds_ = 0.0F;
    float fadeTime_ = 0.0F;
    std::vector<assets::NodeTransform> fadeFrom_;
    std::vector<assets::NodeTransform> sampled_;
    std::vector<assets::NodeTransform> pose_;
    std::vector<glm::mat4> nodeWorld_;
    std::vector<glm::mat4> joints_;
};

} // namespace dwe::animation
