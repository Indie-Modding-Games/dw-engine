#include <dwe/animation/clip_sampling.hpp>
#include <dwe/animation/clip_synthesis.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <span>
#include <utility>

namespace dwe::animation {

namespace {

using assets::AnimationChannel;
using assets::AnimationClip;
using assets::BoneGroups;
using assets::ChannelPath;
using assets::ModelData;
using assets::MotionKind;
using assets::MotionRecipe;
using assets::NodeTransform;
using assets::PartnerClipMode;
using assets::PartnerClipRecipe;

// anim.c: tickAnimation, instruction 0x4000: bank 4 is the entity's own species bank.
constexpr std::uint8_t SpeciesSoundBank = 4;
// A channel whose values all stay this close to the rest pose is left out.
constexpr float RotationEpsilon = 1.0e-5F;
constexpr float TranslationEpsilon = 1.0e-3F;
constexpr float TwoPi = 2.0F * std::numbers::pi_v<float>;

// The parts of the shapes the motions share: how long the wind-ups and the recoveries take, as
// parts of the clip.
constexpr float LieDownPart = 0.3F;
constexpr float SquatPart = 0.25F;
constexpr float GuardPart = 0.3F;
constexpr float RecoilPeakPart = 0.15F;
constexpr float RecoilRecoverPart = 0.6F;
constexpr float FallPart = 0.6F;
constexpr float RisePart = 0.7F;
constexpr float LungeWindUpPart = 0.6F;
constexpr float LungeWindUpBack = 0.4F;
constexpr float LungeHoldPart = 0.15F;
// The arms follow the legs' swing at this part, the other way; the body rolls at this part of it.
constexpr float StrideArmPart = 0.6F;
constexpr float StrideRollPart = 0.1F;
// The breath of a lying body, in hips' heights, and how much faster a squatting one shivers.
constexpr float LieBreath = 0.01F;
constexpr float SquatShiverRate = 6.0F;
constexpr float SquatShiverDegrees = 2.0F;
// A lunge's punch swings the arms this much more than the body bends.
constexpr float LungeArmPart = 1.5F;
// How far an upright body moves for its lean: its top's shift, in parts of the hips' height.
constexpr float UprightShiftPart = 1.0F;

float smooth(float t) {
  const float x = std::clamp(t, 0.0F, 1.0F);
  return x * x * (3.0F - (2.0F * x));
}

glm::quat about(glm::vec3 axis, float degrees) {
  return glm::angleAxis(glm::radians(degrees), axis);
}

const glm::vec3 axisX{1.0F, 0.0F, 0.0F};
const glm::vec3 axisY{0.0F, 1.0F, 0.0F};
const glm::vec3 axisZ{0.0F, 0.0F, 1.0F};

glm::vec4 fromQuat(glm::quat q) {
  return {q.x, q.y, q.z, q.w};
}

std::vector<NodeTransform> restOf(const ModelData& model) {
  std::vector<NodeTransform> pose(model.nodes.size());
  restPose(model, pose);
  return pose;
}

// Every node's rotation in model space.
std::vector<glm::quat> globalRotations(const ModelData& model,
                                       std::span<const NodeTransform> pose) {
  std::vector<glm::quat> out(model.nodes.size(), glm::quat{1.0F, 0.0F, 0.0F, 0.0F});
  for (const std::uint32_t node : model.nodeOrder) {
    const std::optional<std::uint32_t> parent = model.nodes[node].parent;
    out[node] = parent ? out[*parent] * pose[node].rotation : pose[node].rotation;
  }
  return out;
}

// Every node's position in model space.
std::vector<glm::vec3> globalPositions(const ModelData& model,
                                       std::span<const NodeTransform> pose) {
  std::vector<glm::vec3> out(model.nodes.size(), glm::vec3{0.0F});
  const std::vector<glm::quat> rotations = globalRotations(model, pose);
  for (const std::uint32_t node : model.nodeOrder) {
    const std::optional<std::uint32_t> parent = model.nodes[node].parent;
    out[node] = parent ? out[*parent] + (rotations[*parent] * pose[node].translation)
                       : pose[node].translation;
  }
  return out;
}

// The keyframe times of every channel of a clip, in order, once each.
std::vector<float> timesOf(const AnimationClip& clip) {
  std::vector<float> times;
  for (const AnimationChannel& channel : clip.channels) {
    times.insert(times.end(), channel.times.begin(), channel.times.end());
  }
  std::ranges::sort(times);
  const auto [first, last] = std::ranges::unique(times);
  times.erase(first, last);
  if (times.empty()) {
    times.push_back(0.0F);
  }
  return times;
}

const AnimationChannel* findChannel(const AnimationClip& clip, std::uint32_t node,
                                    ChannelPath path) {
  const auto found = std::ranges::find_if(clip.channels, [&](const AnimationChannel& channel) {
    return channel.node == node && channel.path == path;
  });
  return found != clip.channels.end() ? &*found : nullptr;
}

// The model's joints and nodes, and what a skeleton's numbers are.
struct Skeleton {
  const ModelData* model = nullptr;
  // The model's joints (its skin's): skeletonOf takes only a model with a skin.
  std::span<const std::uint32_t> skinJoints;
  std::vector<NodeTransform> rest;
  std::vector<glm::quat> restGlobal;
  // The node of the skin's joint 0 (the root, whose translation is the movement) and of joint 1
  // (the hips), when the skin has them.
  std::optional<std::uint32_t> root;
  std::optional<std::uint32_t> hips;
  float hipsHeight = 0.0F;

  [[nodiscard]] std::size_t joints() const { return skinJoints.size(); }
  [[nodiscard]] std::uint32_t nodeOf(std::uint32_t joint) const { return skinJoints[joint]; }
  [[nodiscard]] std::optional<std::uint32_t> jointOf(std::uint32_t node) const {
    const auto found = std::ranges::find(skinJoints, node);
    if (found == skinJoints.end()) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(found - skinJoints.begin());
  }
};

Skeleton skeletonOf(const ModelData& model, const assets::ModelSkin& skin) {
  Skeleton out{.model = &model,
               .skinJoints = skin.joints,
               .rest = restOf(model),
               .restGlobal = {},
               .root = std::nullopt,
               .hips = std::nullopt,
               .hipsHeight = 0.0F};
  out.restGlobal = globalRotations(model, out.rest);
  if (out.joints() > 0) {
    out.root = out.nodeOf(0);
  }
  if (out.joints() > 1) {
    out.hips = out.nodeOf(1);
    out.hipsHeight = globalPositions(model, out.rest)[*out.hips].y;
  }
  return out;
}

core::Result<void> checkJoint(const Skeleton& skeleton, std::uint32_t joint,
                              std::string_view whose) {
  if (joint >= skeleton.joints()) {
    return core::fail(
        std::format("{} has no joint {} (its skin has {})", whose, joint, skeleton.joints()));
  }
  return {};
}

core::Result<void> checkRecipe(const Skeleton& target, const Skeleton& donor,
                               const PartnerClipRecipe& recipe) {
  if (recipe.sameBones && target.joints() != donor.joints()) {
    return core::fail(std::format("bones: same, but the model has {} joints and the donor {}",
                                  target.joints(), donor.joints()));
  }
  for (const auto& [joint, donorJoint] : recipe.bones) {
    if (auto ok = checkJoint(target, joint, "the model"); !ok) {
      return ok;
    }
    if (auto ok = checkJoint(donor, donorJoint, "the donor"); !ok) {
      return ok;
    }
  }
  if (!recipe.groups) {
    return {};
  }
  const BoneGroups& groups = *recipe.groups;
  std::vector<std::uint32_t> all{groups.body};
  for (const auto* list :
       {&groups.head, &groups.leftArm, &groups.rightArm, &groups.leftLeg, &groups.rightLeg}) {
    all.insert(all.end(), list->begin(), list->end());
  }
  if (groups.spin) {
    all.insert(all.end(), groups.spin->joints.begin(), groups.spin->joints.end());
  }
  for (const std::uint32_t joint : all) {
    if (auto ok = checkJoint(target, joint, "the model"); !ok) {
      return ok;
    }
  }
  return {};
}

// The clip being made: one rotation and one translation value per node and keyframe.
struct Frames {
  std::vector<float> times;
  std::vector<std::vector<NodeTransform>> poses;
};

// Writes the frames as channels: a node whose rotation or translation leaves its rest pose gets a
// channel of it. The root motion node is the donor's root channel, copied as it is.
void writeChannels(const Skeleton& target, const Frames& frames, AnimationClip& clip) {
  for (std::uint32_t node = 0; node < target.rest.size(); ++node) {
    if (target.root && node == *target.root) {
      continue;
    }
    bool turns = false;
    bool moves = false;
    for (const std::vector<NodeTransform>& pose : frames.poses) {
      const float dot = std::abs(glm::dot(pose[node].rotation, target.rest[node].rotation));
      turns = turns || 1.0F - dot > RotationEpsilon;
      moves = moves || glm::length(pose[node].translation - target.rest[node].translation) >
                           TranslationEpsilon;
    }
    const auto channel = [&](ChannelPath path) {
      AnimationChannel out;
      out.node = node;
      out.path = path;
      out.times = frames.times;
      for (const std::vector<NodeTransform>& pose : frames.poses) {
        out.values.push_back(path == ChannelPath::Rotation
                                 ? fromQuat(pose[node].rotation)
                                 : glm::vec4{pose[node].translation, 0.0F});
      }
      return out;
    };
    if (turns) {
      clip.channels.push_back(channel(ChannelPath::Rotation));
    }
    if (moves) {
      clip.channels.push_back(channel(ChannelPath::Translation));
    }
  }
}

// A new clip with the donor clip's id, timing, loops and root motion, and its sounds but the
// species bank's.
AnimationClip shellOf(const Skeleton& target, const AnimationClip& donorClip,
                      std::string_view donor, std::string_view how) {
  AnimationClip clip;
  clip.name = std::format("{} {} of {}", donorClip.name, how, donor);
  clip.id = donorClip.id;
  clip.duration = donorClip.duration;
  clip.ticksPerSecond = donorClip.ticksPerSecond;
  clip.tickCount = donorClip.tickCount;
  clip.frameCount = donorClip.frameCount;
  clip.loops = donorClip.loops;
  clip.endsInLoop = donorClip.endsInLoop;
  clip.repeat = donorClip.repeat;
  for (const assets::ClipSound& sound : donorClip.sounds) {
    if (sound.bank != SpeciesSoundBank) {
      clip.sounds.push_back(sound);
    }
  }
  const std::optional<std::uint32_t> donorRoot = donorClip.rootMotionNode;
  if (donorRoot && target.root) {
    clip.rootMotionNode = target.root;
    if (const AnimationChannel* root =
            findChannel(donorClip, *donorRoot, ChannelPath::Translation)) {
      AnimationChannel copy = *root;
      copy.node = *target.root;
      clip.channels.push_back(std::move(copy));
    }
  }
  return clip;
}

AnimationClip retarget(const Skeleton& target, const Skeleton& donor,
                       const PartnerClipRecipe& recipe, const AnimationClip& donorClip) {
  AnimationClip clip = shellOf(target, donorClip, recipe.donor, "moved from");
  const ModelData& model = *target.model;
  // The donor node each node follows.
  std::vector<std::optional<std::uint32_t>> follows(model.nodes.size());
  for (std::uint32_t joint = 0; joint < target.joints(); ++joint) {
    if (const std::optional<std::uint32_t> donorJoint = recipe.donorJoint(joint)) {
      follows[target.nodeOf(joint)] = donor.nodeOf(*donorJoint);
    }
  }
  const float scale = donor.hipsHeight > 0.0F && target.hipsHeight > 0.0F
                          ? target.hipsHeight / donor.hipsHeight
                          : 1.0F;
  Frames frames{.times = timesOf(donorClip), .poses = {}};
  std::vector<NodeTransform> donorPose(donor.rest.size());
  for (const float time : frames.times) {
    sampleClip(*donor.model, donorClip, time, donorPose);
    const std::vector<glm::quat> donorGlobal = globalRotations(*donor.model, donorPose);
    std::vector<NodeTransform> pose = target.rest;
    std::vector<glm::quat> global(model.nodes.size(), glm::quat{1.0F, 0.0F, 0.0F, 0.0F});
    for (const std::uint32_t node : model.nodeOrder) {
      const std::optional<std::uint32_t> parent = model.nodes[node].parent;
      const glm::quat parentGlobal = parent ? global[*parent] : glm::quat{1.0F, 0.0F, 0.0F, 0.0F};
      if (const std::optional<std::uint32_t> from = follows[node];
          from && (!target.root || node != *target.root)) {
        const glm::quat turn = donorGlobal[*from] * glm::inverse(donor.restGlobal[*from]);
        pose[node].rotation =
            glm::normalize(glm::inverse(parentGlobal) * turn * target.restGlobal[node]);
        // The donor's own bone moves (the hips' bob, a same skeleton's sliding joints), scaled.
        const bool hips = target.hips && donor.hips && node == *target.hips && *from == *donor.hips;
        if (recipe.sameBones || hips) {
          pose[node].translation =
              target.rest[node].translation +
              ((donorPose[*from].translation - donor.rest[*from].translation) * scale);
        }
      }
      global[node] = parentGlobal * pose[node].rotation;
    }
    frames.poses.push_back(std::move(pose));
  }
  writeChannels(target, frames, clip);
  return clip;
}

// How a motion moves the body at one moment of its clip.
struct MotionPose {
  glm::vec3 bodyOffset{0.0F}; // model space
  glm::quat bodyTurn{1.0F, 0.0F, 0.0F, 0.0F};
  float headPitch = 0.0F;
  // Forward swing of each limb, degrees; arms' raise to the side, degrees.
  float leftArm = 0.0F;
  float rightArm = 0.0F;
  float leftLeg = 0.0F;
  float rightLeg = 0.0F;
  float armsRaise = 0.0F;
};

// The lunge's bend at `u`: back on the wind-up, fully forward on the strike, held a moment and
// back.
float lungeBend(float u, float strike) {
  const float windUp = LungeWindUpPart * strike;
  if (u < windUp) {
    return -LungeWindUpBack * smooth(u / windUp);
  }
  if (u < strike) {
    return -LungeWindUpBack + ((1.0F + LungeWindUpBack) * smooth((u - windUp) / (strike - windUp)));
  }
  if (u < strike + LungeHoldPart) {
    return 1.0F;
  }
  const float left = 1.0F - strike - LungeHoldPart;
  return left > 0.0F ? 1.0F - smooth((u - strike - LungeHoldPart) / left) : 1.0F;
}

// The pose a motion adds at `u` (0 at the clip's start, 1 at its end); `height` is the hips'.
MotionPose motionAt(const MotionRecipe& motion, float u, float hipsHeight, bool upright) {
  MotionPose out;
  const auto beats = static_cast<float>(motion.beats);
  const float wave = std::sin(TwoPi * beats * u);
  const float lift = motion.height * hipsHeight;
  float pitch = motion.lean;
  float roll = 0.0F;
  float yaw = 0.0F;
  switch (motion.kind) {
  case MotionKind::Breathe:
    out.bodyOffset.y = lift * wave;
    out.headPitch = motion.angle * wave;
    break;
  case MotionKind::Stride:
    out.bodyOffset.y = lift * (0.5F - (0.5F * std::cos(2.0F * TwoPi * beats * u)));
    out.leftLeg = motion.angle * wave;
    out.rightLeg = -motion.angle * wave;
    out.leftArm = -StrideArmPart * motion.angle * wave;
    out.rightArm = StrideArmPart * motion.angle * wave;
    roll = StrideRollPart * motion.angle * wave;
    break;
  case MotionKind::Bounce: {
    const float hop = std::max(0.0F, wave);
    out.bodyOffset.y = lift * hop;
    out.armsRaise = motion.angle * (0.5F + (0.5F * hop));
    break;
  }
  case MotionKind::Shake:
    yaw = motion.angle * wave;
    out.headPitch = motion.lean;
    break;
  case MotionKind::Nod: {
    const float bend = 0.5F - (0.5F * std::cos(TwoPi * beats * u));
    pitch += motion.angle * bend;
    out.headPitch = 0.5F * motion.angle * bend;
    out.bodyOffset.y = -lift * bend;
    break;
  }
  case MotionKind::Lie: {
    const float down = smooth(u / LieDownPart);
    out.bodyOffset.y = (-lift * down) + (LieBreath * hipsHeight * wave * down);
    roll = motion.angle * down;
    pitch *= down;
    break;
  }
  case MotionKind::Squat: {
    const float down = smooth(u / SquatPart) * smooth((1.0F - u) / SquatPart);
    out.bodyOffset.y = -lift * down;
    out.leftLeg = motion.angle * down;
    out.rightLeg = motion.angle * down;
    roll = SquatShiverDegrees * std::sin(TwoPi * beats * SquatShiverRate * u) * down;
    break;
  }
  case MotionKind::Droop:
    roll = motion.angle * wave;
    out.headPitch = 0.5F * motion.lean;
    break;
  case MotionKind::Guard: {
    const float in = smooth(u / GuardPart);
    out.bodyOffset.y = -lift * in;
    pitch *= in;
    out.leftArm = motion.angle * in;
    out.rightArm = motion.angle * in;
    break;
  }
  case MotionKind::Recoil: {
    const float hit = u < RecoilPeakPart ? smooth(u / RecoilPeakPart)
                                         : 1.0F - smooth((u - RecoilPeakPart) / RecoilRecoverPart);
    pitch += (motion.fromBehind ? motion.angle : -motion.angle) * hit;
    out.bodyOffset.y = -lift * hit;
    break;
  }
  case MotionKind::Fall:
  case MotionKind::Rise: {
    const float down =
        motion.kind == MotionKind::Fall ? smooth(u / FallPart) : 1.0F - smooth(u / RisePart);
    roll = motion.angle * down;
    out.bodyOffset.y = -lift * down;
    pitch *= down;
    break;
  }
  case MotionKind::Lunge: {
    const float bend = lungeBend(u, motion.strike);
    pitch += motion.angle * bend;
    out.bodyOffset.z = lift * std::max(bend, 0.0F);
    out.leftArm = LungeArmPart * motion.angle * std::max(bend, 0.0F);
    out.rightArm = out.leftArm;
    break;
  }
  }
  if (upright) {
    // The lean's top moves the body instead: forward (+z) on a forward lean.
    out.bodyOffset.z += UprightShiftPart * hipsHeight * std::sin(glm::radians(pitch));
    pitch = 0.0F;
  }
  out.bodyTurn = about(axisY, yaw) * about(axisX, pitch) * about(axisZ, roll);
  return out;
}

glm::vec3 spinAxis(assets::SpinAxis axis) {
  switch (axis) {
  case assets::SpinAxis::X:
    return axisX;
  case assets::SpinAxis::Y:
    return axisY;
  case assets::SpinAxis::Z:
    return axisZ;
  }
  return axisX;
}

// The base of a procedural clip: the model's own idle at a time, round its length.
void basePose(const Skeleton& target, const std::optional<AnimationClip>& idle, float time,
              std::vector<NodeTransform>& pose) {
  if (!idle || idle->duration <= 0.0F) {
    pose = target.rest;
    return;
  }
  sampleClip(*target.model, *idle, std::fmod(time, idle->duration), pose);
}

AnimationClip procedural(const Skeleton& target, const PartnerClipRecipe& recipe,
                         const BoneGroups& groups, const MotionRecipe& motion,
                         const AnimationClip& donorClip, const std::optional<AnimationClip>& idle) {
  AnimationClip clip = shellOf(target, donorClip, recipe.donor, "timed by");
  const ModelData& model = *target.model;
  // The model-space turn each node takes on top of its parent's, this frame.
  std::vector<glm::quat> turns(model.nodes.size());
  Frames frames{.times = timesOf(donorClip), .poses = {}};
  const float start = frames.times.front();
  const float length = frames.times.back() - start;
  std::vector<NodeTransform> pose(model.nodes.size());
  for (const float time : frames.times) {
    const float u = length > 0.0F ? (time - start) / length : 0.0F;
    const MotionPose at = motionAt(motion, u, target.hipsHeight, groups.upright);
    std::ranges::fill(turns, glm::quat{1.0F, 0.0F, 0.0F, 0.0F});
    const auto turnAll = [&](const std::vector<std::uint32_t>& joints, glm::quat turn) {
      for (const std::uint32_t joint : joints) {
        turns[target.nodeOf(joint)] = turn;
      }
    };
    // A forward swing takes a hanging limb's end towards +z: a turn the other way about x.
    turnAll(groups.leftArm, about(axisZ, at.armsRaise) * about(axisX, -at.leftArm));
    turnAll(groups.rightArm, about(axisZ, -at.armsRaise) * about(axisX, -at.rightArm));
    turnAll(groups.leftLeg, about(axisX, -at.leftLeg));
    turnAll(groups.rightLeg, about(axisX, -at.rightLeg));
    turnAll(groups.head, about(axisX, at.headPitch));
    const std::uint32_t body = target.nodeOf(groups.body);
    turns[body] = at.bodyTurn;
    basePose(target, idle, time, pose);
    std::vector<glm::quat> global(model.nodes.size(), glm::quat{1.0F, 0.0F, 0.0F, 0.0F});
    for (const std::uint32_t node : model.nodeOrder) {
      const std::optional<std::uint32_t> parent = model.nodes[node].parent;
      const glm::quat parentGlobal = parent ? global[*parent] : glm::quat{1.0F, 0.0F, 0.0F, 0.0F};
      pose[node].rotation = glm::normalize(glm::inverse(parentGlobal) * turns[node] * parentGlobal *
                                           pose[node].rotation);
      if (node == body) {
        pose[node].translation += glm::inverse(parentGlobal) * at.bodyOffset;
      }
      global[node] = parentGlobal * pose[node].rotation;
    }
    if (groups.spin) {
      const float degrees = 360.0F * groups.spin->turnsPerSecond * time;
      for (const std::uint32_t joint : groups.spin->joints) {
        NodeTransform& spun = pose[target.nodeOf(joint)];
        spun.rotation = spun.rotation * about(spinAxis(groups.spin->axis), degrees);
      }
    }
    if (target.root) {
      pose[*target.root] = target.rest[*target.root];
    }
    frames.poses.push_back(pose);
  }
  writeChannels(target, frames, clip);
  return clip;
}

// Donor joint -> this model's joint, for the effects' points: the joint mapped to it, else the
// one mapped to its nearest mapped ancestor, else the root.
std::map<std::int32_t, std::int32_t> effectJointsOf(const Skeleton& donor,
                                                    const PartnerClipRecipe& recipe) {
  std::map<std::int32_t, std::int32_t> out;
  if (recipe.sameBones) {
    return out;
  }
  std::map<std::uint32_t, std::uint32_t> mappedTo;
  for (const auto& [joint, donorJoint] : recipe.bones) {
    mappedTo.try_emplace(donorJoint, joint);
  }
  for (std::uint32_t donorJoint = 0; donorJoint < donor.joints(); ++donorJoint) {
    std::optional<std::uint32_t> walk = donorJoint;
    std::uint32_t joint = 0;
    while (walk) {
      if (const auto found = mappedTo.find(*walk); found != mappedTo.end()) {
        joint = found->second;
        break;
      }
      const std::optional<std::uint32_t> parent = donor.model->nodes[donor.nodeOf(*walk)].parent;
      walk = parent ? donor.jointOf(*parent) : std::nullopt;
    }
    if (joint != donorJoint) {
      out.emplace(static_cast<std::int32_t>(donorJoint), static_cast<std::int32_t>(joint));
    }
  }
  return out;
}

} // namespace

core::Result<PartnerClipsMade> addPartnerClips(ModelData& model, const ModelData& donor,
                                               const assets::PartnerClipBook& book,
                                               const PartnerClipRecipe& recipe) {
  if (!model.skin || !donor.skin) {
    return core::fail("a model without a skin gets no partner clips");
  }
  const Skeleton target = skeletonOf(model, *model.skin);
  const Skeleton from = skeletonOf(donor, *donor.skin);
  if (auto checked = checkRecipe(target, from, recipe); !checked) {
    return std::unexpected{checked.error()};
  }
  std::vector<std::uint32_t> wanted = book.clips;
  for (const AnimationClip& clip : donor.clips) {
    if (clip.id && *clip.id >= book.firstTechnique) {
      wanted.push_back(*clip.id);
    }
  }
  std::ranges::sort(wanted);
  const auto [first, last] = std::ranges::unique(wanted);
  wanted.erase(first, last);

  std::optional<AnimationClip> idle;
  if (const AnimationClip* own = model.findClip(std::uint32_t{0})) {
    idle = *own;
  }
  PartnerClipsMade out;
  std::vector<AnimationClip> made;
  for (const std::uint32_t id : wanted) {
    if (model.findClip(id) != nullptr && std::ranges::contains(recipe.keep, id)) {
      continue;
    }
    const AnimationClip* donorClip = donor.findClip(id);
    if (donorClip == nullptr) {
      out.unmade.push_back(id);
      continue;
    }
    if (recipe.mode == PartnerClipMode::Retarget) {
      made.push_back(retarget(target, from, recipe, *donorClip));
    } else if (const std::optional<MotionRecipe> motion = book.motionOf(recipe, id);
               motion && recipe.groups) {
      made.push_back(procedural(target, recipe, *recipe.groups, *motion, *donorClip, idle));
    } else {
      out.unmade.push_back(id);
      continue;
    }
    out.made.push_back(id);
  }
  // The target skeleton points into the model: the clips change only once all are made.
  std::erase_if(model.clips, [&](const AnimationClip& clip) {
    return clip.id && std::ranges::contains(out.made, *clip.id);
  });
  for (AnimationClip& clip : made) {
    model.clips.push_back(std::move(clip));
  }
  out.effectJoints = effectJointsOf(from, recipe);
  return out;
}

} // namespace dwe::animation
