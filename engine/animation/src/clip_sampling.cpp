#include <dwe/animation/clip_sampling.hpp>
#include <dwe/core/assert.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace dwe::animation {

namespace {

using assets::AnimationChannel;
using assets::AnimationClip;
using assets::ChannelPath;
using assets::Interpolation;
using assets::ModelData;
using assets::NodeTransform;

glm::quat toQuat(glm::vec4 v) {
    return glm::quat{v.w, v.x, v.y, v.z};
}

// The channel's value at a keyframe time, held before the first keyframe and after the last.
glm::vec4 sampleChannel(const AnimationChannel& channel, double seconds) {
    const auto& times = channel.times;
    const auto after = std::ranges::upper_bound(times, seconds, {}, [](float t) { return static_cast<double>(t); });
    if (after == times.begin()) {
        return channel.values.front();
    }
    if (after == times.end()) {
        return channel.values.back();
    }
    const auto i = static_cast<std::size_t>(std::distance(times.begin(), after)) - 1;
    const glm::vec4 a = channel.values[i];
    if (channel.interpolation == Interpolation::Step) {
        return a;
    }
    const glm::vec4 b = channel.values[i + 1];
    const double span = static_cast<double>(times[i + 1]) - static_cast<double>(times[i]);
    const auto t = static_cast<float>((seconds - static_cast<double>(times[i])) / span);
    if (channel.path == ChannelPath::Rotation) {
        const glm::quat q = glm::slerp(toQuat(a), toQuat(b), t);
        return {q.x, q.y, q.z, q.w};
    }
    return glm::mix(a, b, t);
}

glm::vec4 sampleAt(const AnimationChannel& channel, const ClipPosition& at) {
    const glm::vec4 value = sampleChannel(channel, at.seconds);
    if (at.blend <= 0.0F) {
        return value;
    }
    const glm::vec4 towards = sampleChannel(channel, at.towards);
    if (channel.path == ChannelPath::Rotation) {
        const glm::quat q = glm::slerp(toQuat(value), toQuat(towards), at.blend);
        return {q.x, q.y, q.z, q.w};
    }
    return glm::mix(value, towards, at.blend);
}

const AnimationChannel* rootChannel(const AnimationClip& clip) {
    if (!clip.rootMotionNode) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(clip.channels, [&clip](const AnimationChannel& channel) {
        return channel.node == *clip.rootMotionNode && channel.path == ChannelPath::Translation;
    });
    return found != clip.channels.end() ? &*found : nullptr;
}

// The root's offset from rest at a keyframe time.
glm::vec3 rootOffset(const ModelData& model, const AnimationChannel& channel, double seconds) {
    return glm::vec3{sampleChannel(channel, seconds)} - model.nodes[channel.node].rest.translation;
}

// How far one round of the repeat moves the root: from the tick before its start to its end.
glm::vec3 repeatDistance(const ModelData& model, const assets::ClipRepeat& repeat, const AnimationChannel& channel) {
    return rootOffset(model, channel, repeat.end) -
           rootOffset(model, channel, static_cast<double>(repeat.start) - static_cast<double>(repeat.gap));
}

} // namespace

ClipPosition locate(const AnimationClip& clip, double seconds) {
    seconds = std::max(seconds, 0.0);
    if (!clip.repeat) {
        return {.seconds = std::min(seconds, static_cast<double>(clip.duration))};
    }
    const auto start = static_cast<double>(clip.repeat->start);
    const auto end = static_cast<double>(clip.repeat->end);
    const auto gap = static_cast<double>(clip.repeat->gap);
    if (seconds <= end) {
        return {.seconds = seconds};
    }
    const double period = end - start + gap;
    if (period <= 0.0) {
        return {.seconds = end};
    }
    const double rounds = std::floor((seconds - start) / period);
    const double local = start + (seconds - start - (rounds * period));
    const auto repeats = static_cast<std::uint64_t>(rounds);
    if (local <= end || gap <= 0.0) {
        return {.seconds = std::min(local, end), .repeats = repeats};
    }
    return {.seconds = end, .towards = start, .blend = static_cast<float>((local - end) / gap), .repeats = repeats};
}

void restPose(const ModelData& model, std::span<NodeTransform> pose) {
    DWE_ASSERT(pose.size() == model.nodes.size());
    for (std::size_t n = 0; n < pose.size(); ++n) {
        pose[n] = model.nodes[n].rest;
    }
}

void sampleClip(const ModelData& model, const AnimationClip& clip, double seconds, std::span<NodeTransform> pose) {
    restPose(model, pose);
    const ClipPosition at = locate(clip, seconds);
    for (const AnimationChannel& channel : clip.channels) {
        NodeTransform& node = pose[channel.node];
        switch (channel.path) {
        case ChannelPath::Translation:
            if (clip.rootMotionNode != channel.node) {
                node.translation = glm::vec3{sampleAt(channel, at)};
            }
            break;
        case ChannelPath::Rotation:
            node.rotation = glm::normalize(toQuat(sampleAt(channel, at)));
            break;
        case ChannelPath::Scale:
            node.scale = glm::vec3{sampleAt(channel, at)};
            break;
        }
    }
}

glm::vec3 rootMotion(const ModelData& model, const AnimationClip& clip, double seconds) {
    const AnimationChannel* channel = rootChannel(clip);
    if (channel == nullptr) {
        return glm::vec3{0.0F};
    }
    const ClipPosition at = locate(clip, seconds);
    if (!clip.repeat) {
        return rootOffset(model, *channel, at.seconds);
    }
    const glm::vec3 distance = repeatDistance(model, *clip.repeat, *channel);
    glm::vec3 offset = rootOffset(model, *channel, at.seconds);
    if (at.blend > 0.0F) {
        // The step back to the start moves as the step into it did.
        offset = glm::mix(offset, rootOffset(model, *channel, at.towards) + distance, at.blend);
    }
    return offset + (distance * static_cast<float>(at.repeats));
}

float repeatGroundSpeed(const ModelData& model, const AnimationClip& clip) {
    const AnimationChannel* channel = rootChannel(clip);
    if (channel == nullptr || !clip.repeat) {
        return 0.0F;
    }
    const float period = clip.repeat->end - clip.repeat->start + clip.repeat->gap;
    if (period <= 0.0F) {
        return 0.0F;
    }
    const glm::vec3 distance = repeatDistance(model, *clip.repeat, *channel);
    return glm::length(glm::vec2{distance.x, distance.z}) / period;
}

void blendPoses(std::span<const NodeTransform> from, std::span<const NodeTransform> to, float t,
                std::span<NodeTransform> out) {
    DWE_ASSERT(from.size() == to.size() && out.size() == to.size());
    for (std::size_t n = 0; n < out.size(); ++n) {
        out[n] = NodeTransform{.translation = glm::mix(from[n].translation, to[n].translation, t),
                               .rotation = glm::slerp(from[n].rotation, to[n].rotation, t),
                               .scale = glm::mix(from[n].scale, to[n].scale, t)};
    }
}

std::size_t jointCount(const ModelData& model) {
    return model.skin ? model.skin->joints.size() : 1;
}

void computeJointMatrices(const ModelData& model, std::span<const NodeTransform> pose, std::span<glm::mat4> nodeWorld,
                          std::span<glm::mat4> out) {
    DWE_ASSERT(pose.size() == model.nodes.size() && nodeWorld.size() == model.nodes.size() &&
               out.size() == jointCount(model));
    if (!model.skin) {
        out[0] = glm::mat4{1.0F};
        return;
    }
    const std::span<glm::mat4> world = nodeWorld;
    for (const std::uint32_t n : model.nodeOrder) {
        const auto& parent = model.nodes[n].parent;
        world[n] = parent ? world[*parent] * pose[n].matrix() : pose[n].matrix();
    }
    for (std::size_t j = 0; j < out.size(); ++j) {
        out[j] = world[model.skin->joints[j]] * model.skin->inverseBindMatrices[j];
    }
}

} // namespace dwe::animation
