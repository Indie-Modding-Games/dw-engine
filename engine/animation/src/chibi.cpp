#include <dwe/animation/chibi.hpp>
#include <dwe/animation/clip_sampling.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>
#include <ranges>
#include <system_error>
#include <tuple>
#include <utility>

namespace dwe::animation {

namespace {

using assets::ModelData;
using assets::NodeTransform;

constexpr float Infinity = std::numeric_limits<float>::infinity();
// A branch counts as a limb (for where the trunk ends) with this share of the vertices.
constexpr float LimbShare = 0.04F;
// A head has at least this share of the vertices.
constexpr float HeadShare = 0.05F;
// Within this share of the half width a branch is in the middle of the body.
constexpr float MiddleShare = 0.25F;
// A leg's lowest vertex is within this share of the height of the model's lowest.
constexpr float GroundShare = 0.0625F;
// A leg's centroid is below where it hangs by this share of how far down it reaches.
constexpr float LegDrop = 0.35F;
// A leg goes down more than this times sideways (to the joint at its end).
constexpr float LegSlope = 0.8F;
// A leg hangs from the lower part of the body: at most this share of the height above the ground.
constexpr float LegAttach = 0.6F;
// An arm hangs: its centroid is this share of the height below where it hangs; a wing does not.
constexpr float ArmDrop = 0.1F;
// A wing that goes back: its centroid is this share of the height behind where it hangs.
constexpr float WingBack = 0.15F;
// Two joints closer than this (model units) are at the same place.
constexpr float SamePlace = 1.0F;
constexpr float MinFactor = 0.25F;
constexpr float MaxFactor = 4.0F;

struct Bone {
  std::optional<std::uint32_t> parent;
  std::vector<std::uint32_t> children;
  glm::vec3 joint{0.0F};
  std::uint32_t verts = 0;
  glm::vec3 sum{0.0F};
  // The whole branch from this bone down.
  std::uint32_t treeVerts = 0;
  std::uint32_t treeMeshBones = 0;
  glm::vec3 treeSum{0.0F};
  glm::vec3 treeLowest{0.0F, Infinity, 0.0F};
};

struct Skeleton {
  std::vector<Bone> bones;
  // Parents before children.
  std::vector<std::uint32_t> order;
  std::vector<std::uint32_t> bodyRoots;
  std::uint32_t totalVerts = 0;
  float ground = 0.0F;
  float height = 1.0F;
  float halfWidth = 1.0F;
};

Skeleton readSkeleton(const ModelData& model) {
  Skeleton skeleton;
  const assets::ModelSkin& skin = *model.skin;
  skeleton.bones.resize(skin.joints.size());
  std::vector<std::optional<std::uint32_t>> jointOf(model.nodes.size());
  for (std::uint32_t j = 0; j < skin.joints.size(); ++j) {
    jointOf[skin.joints[j]] = j;
    skeleton.bones[j].joint = glm::vec3{glm::inverse(skin.inverseBindMatrices[j])[3]};
  }
  for (const std::uint32_t node : model.nodeOrder) {
    const auto joint = jointOf[node];
    if (!joint) {
      continue;
    }
    skeleton.order.push_back(*joint);
    // The nearest joint above it.
    for (auto up = model.nodes[node].parent; up; up = model.nodes[*up].parent) {
      if (jointOf[*up]) {
        skeleton.bones[*joint].parent = jointOf[*up];
        skeleton.bones[*jointOf[*up]].children.push_back(*joint);
        break;
      }
    }
  }
  float lowest = Infinity;
  float highest = -Infinity;
  float side = 0.0F;
  for (const assets::ModelPrimitive& primitive : model.primitives) {
    for (const assets::ModelVertex& vertex : primitive.vertices) {
      const std::uint16_t joint = vertex.joints[0];
      if (joint >= skeleton.bones.size()) {
        continue;
      }
      Bone& bone = skeleton.bones[joint];
      ++bone.verts;
      bone.sum += vertex.position;
      if (vertex.position.y < bone.treeLowest.y) {
        bone.treeLowest = vertex.position;
      }
      lowest = std::min(lowest, vertex.position.y);
      highest = std::max(highest, vertex.position.y);
      side = std::max(side, std::abs(vertex.position.x));
      ++skeleton.totalVerts;
    }
  }
  for (const std::uint32_t joint : std::views::reverse(skeleton.order)) {
    Bone& bone = skeleton.bones[joint];
    bone.treeVerts += bone.verts;
    bone.treeSum += bone.sum;
    bone.treeMeshBones += bone.verts > 0 ? 1 : 0;
    if (bone.parent) {
      Bone& parent = skeleton.bones[*bone.parent];
      parent.treeVerts += bone.treeVerts;
      parent.treeSum += bone.treeSum;
      parent.treeMeshBones += bone.treeMeshBones;
      if (bone.treeLowest.y < parent.treeLowest.y) {
        parent.treeLowest = bone.treeLowest;
      }
    }
  }
  if (skeleton.totalVerts > 0) {
    skeleton.ground = lowest;
    skeleton.height = std::max(highest - lowest, 1.0F);
    skeleton.halfWidth = std::max(side, 1.0F);
  }
  // Bone 0 is the entity's root and the bones under it the body's (model.c, the
  // object setup); a root with a mesh of its own is the body too.
  for (const std::uint32_t joint : skeleton.order) {
    const Bone& bone = skeleton.bones[joint];
    if (bone.parent) {
      continue;
    }
    if (bone.verts == 0 && !bone.children.empty()) {
      std::ranges::copy(bone.children, std::back_inserter(skeleton.bodyRoots));
    } else {
      skeleton.bodyRoots.push_back(joint);
    }
  }
  return skeleton;
}

glm::vec3 centroid(const Bone& bone) {
  return bone.treeVerts > 0 ? bone.treeSum / static_cast<float>(bone.treeVerts) : bone.joint;
}

// Every joint of the branch from `root` down (root first).
std::vector<std::uint32_t> branchOf(const Skeleton& skeleton, std::uint32_t root) {
  std::vector<std::uint32_t> branch{root};
  for (std::size_t i = 0; i < branch.size(); ++i) {
    std::ranges::copy(skeleton.bones[branch[i]].children, std::back_inserter(branch));
  }
  return branch;
}

bool isUnder(const Skeleton& skeleton, std::uint32_t joint, std::uint32_t root) {
  for (std::optional<std::uint32_t> at = joint; at; at = skeleton.bones[*at].parent) {
    if (*at == root) {
      return true;
    }
  }
  return false;
}

// The end of a branch: down its biggest child, bone after bone, to the last one with a mesh.
std::uint32_t tipOf(const Skeleton& skeleton, std::uint32_t root) {
  std::uint32_t tip = root;
  std::uint32_t at = root;
  for (;;) {
    const auto& children = skeleton.bones[at].children;
    const auto next = std::ranges::max_element(
        children, {}, [&](std::uint32_t child) { return skeleton.bones[child].treeVerts; });
    if (next == children.end() || skeleton.bones[*next].treeVerts == 0) {
      return tip;
    }
    at = *next;
    if (skeleton.bones[at].verts > 0) {
      tip = at;
    }
  }
}

struct Branch {
  std::uint32_t root = 0;
  BodyPart part = BodyPart::None;
};

struct Detected {
  ChibiClassification classification;
  Skeleton skeleton;
  std::vector<bool> trunk;
  std::vector<Branch> branches;
};

Detected detect(const ModelData& model, const ChibiOverride* override) {
  Detected found;
  found.skeleton = readSkeleton(model);
  const Skeleton& skeleton = found.skeleton;
  const std::size_t count = skeleton.bones.size();
  ChibiClassification& out = found.classification;
  out.parts.assign(count, BodyPart::None);
  found.trunk.assign(count, false);
  if (skeleton.totalVerts == 0) {
    return found;
  }
  const auto total = static_cast<float>(skeleton.totalVerts);
  const auto isLimb = [&](std::uint32_t joint) {
    const Bone& bone = skeleton.bones[joint];
    return bone.treeMeshBones >= 2 && static_cast<float>(bone.treeVerts) >= LimbShare * total;
  };
  // Where two limbs part, and every bone with such a place under it.
  std::vector<bool> parting(count, false);
  for (const std::uint32_t joint : std::views::reverse(skeleton.order)) {
    const Bone& bone = skeleton.bones[joint];
    const auto limbs = std::ranges::count_if(bone.children, isLimb);
    parting[joint] = parting[joint] || limbs >= 2;
    if (bone.parent && parting[joint]) {
      parting[*bone.parent] = true;
    }
  }
  for (const std::uint32_t joint : skeleton.order) {
    const Bone& bone = skeleton.bones[joint];
    const bool bodyRoot = std::ranges::contains(skeleton.bodyRoots, joint);
    if (bodyRoot || (bone.parent && found.trunk[*bone.parent] && parting[joint])) {
      found.trunk[joint] = true;
      out.parts[joint] = BodyPart::Trunk;
    }
  }
  glm::vec3 trunkSum{0.0F};
  std::uint32_t trunkVerts = 0;
  for (std::uint32_t joint = 0; joint < count; ++joint) {
    if (found.trunk[joint]) {
      trunkSum += skeleton.bones[joint].sum;
      trunkVerts += skeleton.bones[joint].verts;
      for (const std::uint32_t child : skeleton.bones[joint].children) {
        if (!found.trunk[child] && skeleton.bones[child].treeVerts > 0) {
          found.branches.push_back({.root = child});
        }
      }
    }
  }
  const glm::vec3 trunkCentre = trunkVerts > 0 ? trunkSum / static_cast<float>(trunkVerts)
                                : skeleton.bodyRoots.empty()
                                    ? glm::vec3{0.0F}
                                    : skeleton.bones[skeleton.bodyRoots.front()].joint;
  const float height = skeleton.height;

  for (Branch& branch : found.branches) {
    const Bone& bone = skeleton.bones[branch.root];
    const glm::vec3 attach = bone.joint;
    const glm::vec3 centre = centroid(bone);
    const glm::vec3 low = bone.treeLowest;
    const glm::vec3 tip = skeleton.bones[tipOf(skeleton, branch.root)].joint;
    const float down = attach.y - low.y;
    const float sideways = glm::length(glm::vec2{tip.x - attach.x, tip.z - attach.z});
    if (bone.treeMeshBones >= 2 && low.y <= skeleton.ground + GroundShare * height &&
        attach.y <= skeleton.ground + LegAttach * height && down >= LegSlope * sideways &&
        attach.y - centre.y >= LegDrop * down) {
      branch.part = BodyPart::Leg;
    }
  }
  // The head: the branch in the middle, highest and most forward of the trunk.
  std::optional<std::size_t> headBranch;
  float best = 0.0F;
  for (std::size_t b = 0; b < found.branches.size(); ++b) {
    const Branch& branch = found.branches[b];
    const Bone& bone = skeleton.bones[branch.root];
    const glm::vec3 centre = centroid(bone);
    if (branch.part != BodyPart::None || static_cast<float>(bone.treeVerts) < HeadShare * total ||
        std::abs(centre.x) > MiddleShare * skeleton.halfWidth) {
      continue;
    }
    const float score = ((centre.y - trunkCentre.y) + (centre.z - trunkCentre.z)) / height;
    if (score > best) {
      best = score;
      headBranch = b;
    }
  }
  for (std::size_t b = 0; b < found.branches.size(); ++b) {
    Branch& branch = found.branches[b];
    const Bone& bone = skeleton.bones[branch.root];
    if (b == headBranch) {
      branch.part = BodyPart::Head;
      continue;
    }
    if (branch.part != BodyPart::None) {
      continue;
    }
    if (bone.treeMeshBones < 2) {
      branch.part = BodyPart::Other;
      continue;
    }
    const glm::vec3 attach = bone.joint;
    const glm::vec3 centre = centroid(bone);
    if (std::abs(centre.x) > MiddleShare * skeleton.halfWidth) {
      const bool hangs = centre.y < attach.y - ArmDrop * height;
      const bool back = centre.z < attach.z - WingBack * height;
      branch.part = hangs && !back ? BodyPart::Arm : BodyPart::Wing;
    } else {
      branch.part = centre.z < trunkCentre.z ? BodyPart::Tail : BodyPart::Other;
    }
  }
  if (override != nullptr) {
    for (Branch& branch : found.branches) {
      if (const auto part = override->parts.find(branch.root); part != override->parts.end()) {
        branch.part = part->second;
      }
    }
  }

  // The head bone: the override's, or the one with the most vertices of the head's branch.
  if (override != nullptr && override->head && *override->head < count) {
    out.head = *override->head;
    for (Branch& branch : found.branches) {
      if (branch.part == BodyPart::Head && !isUnder(skeleton, *out.head, branch.root)) {
        branch.part = BodyPart::Other;
      }
      if (isUnder(skeleton, *out.head, branch.root)) {
        branch.part = BodyPart::Head;
      }
    }
  }
  for (const Branch& branch : found.branches) {
    const std::vector<std::uint32_t> joints = branchOf(skeleton, branch.root);
    if (branch.part == BodyPart::Head && !out.head) {
      out.head = *std::ranges::max_element(
          joints, {}, [&](std::uint32_t joint) { return skeleton.bones[joint].verts; });
    }
    for (const std::uint32_t joint : joints) {
      out.parts[joint] = branch.part;
    }
    switch (branch.part) {
    case BodyPart::Leg:
      ++out.legs;
      break;
    case BodyPart::Arm:
      ++out.arms;
      break;
    case BodyPart::Wing:
      ++out.wings;
      break;
    case BodyPart::Tail:
      ++out.tails;
      break;
    default:
      break;
    }
  }
  if (out.head) {
    // The bones between the trunk and the head are its neck; the head carries the rest.
    for (const Branch& branch : found.branches) {
      if (isUnder(skeleton, *out.head, branch.root)) {
        for (const std::uint32_t joint : branchOf(skeleton, branch.root)) {
          out.parts[joint] = BodyPart::Other;
        }
        for (const std::uint32_t joint : branchOf(skeleton, *out.head)) {
          out.parts[joint] = BodyPart::Head;
        }
        if (branch.root != *out.head) {
          for (auto up = skeleton.bones[*out.head].parent; up; up = skeleton.bones[*up].parent) {
            out.parts[*up] = BodyPart::Neck;
            if (*up == branch.root) {
              break;
            }
          }
        }
      }
    }
  }

  if (out.legs >= 4) {
    out.shape = BodyShape::Quadruped;
  } else if (out.legs == 0 && out.tails > 0) {
    out.shape = BodyShape::Serpent;
  } else if (out.wings >= 2 && out.arms == 0) {
    out.shape = BodyShape::Winged;
  } else if (out.legs >= 2 || out.arms >= 2) {
    out.shape = BodyShape::Biped;
  }
  if (override != nullptr && override->shape) {
    out.shape = *override->shape;
  }
  return found;
}

float factorOf(const ChibiProportions& proportions, BodyPart part) {
  switch (part) {
  case BodyPart::Neck:
    return proportions.neck;
  case BodyPart::Arm:
    return proportions.arm;
  case BodyPart::Leg:
    return proportions.leg;
  case BodyPart::Tail:
    return proportions.tail;
  case BodyPart::Wing:
    return proportions.wing;
  default:
    return 1.0F;
  }
}

// Scales by `along` in the direction `axis` (unit) and by `across` in the others.
glm::mat3 axisScale(glm::vec3 axis, float along, float across) {
  const glm::mat3 projection = glm::outerProduct(axis, axis);
  return (across * (glm::mat3{1.0F} - projection)) + (along * projection);
}

// A model-space direction in a joint's own space (the bind pose's).
glm::vec3 inJointSpace(const assets::ModelSkin& skin, std::uint32_t joint, glm::vec3 direction) {
  const glm::vec3 local = glm::mat3{skin.inverseBindMatrices[joint]} * direction;
  const float length = glm::length(local);
  return length > 0.0F ? local / length : glm::vec3{0.0F};
}

// Whether a bone of part `child` under one of part `parent` goes on with the same limb (a neck goes
// on with its head).
bool continues(BodyPart parent, BodyPart child) {
  return parent == child || (parent == BodyPart::Neck && child == BodyPart::Head);
}

// Where a limb segment points: to the first joint under it, down its longest branch, that is not
// at its own place; or to its own mesh's centre.
std::optional<glm::vec3> segmentDirection(const Skeleton& skeleton, std::uint32_t joint,
                                          BodyPart part, const std::vector<BodyPart>& parts) {
  const Bone& bone = skeleton.bones[joint];
  std::optional<std::uint32_t> at = joint;
  while (at) {
    std::optional<std::uint32_t> next;
    for (const std::uint32_t child : skeleton.bones[*at].children) {
      if (continues(part, parts[child]) &&
          (!next || skeleton.bones[child].treeVerts > skeleton.bones[*next].treeVerts)) {
        next = child;
      }
    }
    if (next && glm::length(skeleton.bones[*next].joint - bone.joint) > SamePlace) {
      return glm::normalize(skeleton.bones[*next].joint - bone.joint);
    }
    at = next;
  }
  if (bone.verts > 0) {
    const glm::vec3 centre = bone.sum / static_cast<float>(bone.verts);
    if (glm::length(centre - bone.joint) > SamePlace) {
      return glm::normalize(centre - bone.joint);
    }
  }
  return std::nullopt;
}

bool hasChildIn(const Skeleton& skeleton, std::uint32_t joint, BodyPart part,
                const std::vector<BodyPart>& parts) {
  return std::ranges::any_of(skeleton.bones[joint].children, [&](std::uint32_t child) {
    return continues(part, parts[child]) && skeleton.bones[child].treeVerts > 0;
  });
}

std::unexpected<core::Error> failAt(const core::YamlNode& node, std::string_view problem) {
  return core::fail(std::format("line {}, column {}: {}", node.line(), node.column(), problem));
}

core::Result<float> readFactor(const core::YamlNode& node) {
  float value = 0.0F;
  if (node.isScalar()) {
    const std::string& text = node.text();
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc{} && end == text.data() + text.size() && value >= MinFactor &&
        value <= MaxFactor) {
      return value;
    }
  }
  return failAt(node, std::format("a factor must be a number from {} to {}", MinFactor, MaxFactor));
}

core::Result<std::uint32_t> readBone(const core::YamlNode& node, std::string_view text) {
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error == std::errc{} && end == text.data() + text.size() && !text.empty() && value < 256) {
    return value;
  }
  return failAt(node, "a bone must be a whole number from 0 to 255");
}

float* proportionNamed(ChibiProportions& proportions, std::string_view name) {
  const std::array<std::pair<std::string_view, float*>, 10> named{{
      {"head", &proportions.head},
      {"torso", &proportions.torso},
      {"neck", &proportions.neck},
      {"arm", &proportions.arm},
      {"leg", &proportions.leg},
      {"hand", &proportions.hand},
      {"foot", &proportions.foot},
      {"tail", &proportions.tail},
      {"wing", &proportions.wing},
      {"girth", &proportions.girth},
  }};
  const auto found = std::ranges::find(named, name, &std::pair<std::string_view, float*>::first);
  return found == named.end() ? nullptr : found->second;
}

core::Result<ChibiOverride> readOverride(const core::YamlNode& node) {
  if (!node.isMapping()) {
    return failAt(node, "a species' override must be a mapping");
  }
  ChibiOverride override;
  ChibiProportions check;
  for (const core::YamlEntry& entry : node.entries()) {
    if (entry.key == "off") {
      if (!entry.value.isScalar() ||
          (entry.value.text() != "true" && entry.value.text() != "false")) {
        return failAt(entry.value, "'off' must be true or false");
      }
      override.off = entry.value.text() == "true";
    } else if (entry.key == "shape") {
      override.shape = entry.value.isScalar() ? bodyShapeNamed(entry.value.text()) : std::nullopt;
      if (!override.shape) {
        return failAt(entry.value, "'shape' must be biped, quadruped, winged, serpent or unknown");
      }
    } else if (entry.key == "head") {
      auto bone = readBone(entry.value, entry.value.isScalar() ? entry.value.text() : "");
      if (!bone) {
        return std::unexpected{bone.error()};
      }
      override.head = *bone;
    } else if (entry.key == "parts") {
      if (!entry.value.isMapping()) {
        return failAt(entry.value, "'parts' must map bones to parts");
      }
      for (const core::YamlEntry& part : entry.value.entries()) {
        auto bone = readBone(part.value, part.key);
        if (!bone) {
          return std::unexpected{bone.error()};
        }
        const auto named = part.value.isScalar() ? bodyPartNamed(part.value.text()) : std::nullopt;
        if (!named) {
          return failAt(part.value, "a part must be head, neck, arm, leg, tail, wing or other");
        }
        override.parts[*bone] = *named;
      }
    } else if (entry.key == "scale") {
      if (!entry.value.isMapping()) {
        return failAt(entry.value, "'scale' must map parts to factors");
      }
      for (const core::YamlEntry& factor : entry.value.entries()) {
        if (proportionNamed(check, factor.key) == nullptr) {
          return failAt(factor.value, std::format("unknown proportion '{}'", factor.key));
        }
        auto value = readFactor(factor.value);
        if (!value) {
          return std::unexpected{value.error()};
        }
        override.scale[factor.key] = *value;
      }
    } else {
      return failAt(entry.value, std::format("unknown key '{}'", entry.key));
    }
  }
  return override;
}

} // namespace

std::string_view nameOf(BodyShape shape) {
  switch (shape) {
  case BodyShape::Biped:
    return "biped";
  case BodyShape::Quadruped:
    return "quadruped";
  case BodyShape::Winged:
    return "winged";
  case BodyShape::Serpent:
    return "serpent";
  case BodyShape::Unknown:
    break;
  }
  return "unknown";
}

std::string_view nameOf(BodyPart part) {
  switch (part) {
  case BodyPart::Trunk:
    return "trunk";
  case BodyPart::Neck:
    return "neck";
  case BodyPart::Head:
    return "head";
  case BodyPart::Arm:
    return "arm";
  case BodyPart::Leg:
    return "leg";
  case BodyPart::Tail:
    return "tail";
  case BodyPart::Wing:
    return "wing";
  case BodyPart::Other:
    return "other";
  case BodyPart::None:
    break;
  }
  return "none";
}

std::optional<BodyShape> bodyShapeNamed(std::string_view name) {
  for (const BodyShape shape : {BodyShape::Unknown, BodyShape::Biped, BodyShape::Quadruped,
                                BodyShape::Winged, BodyShape::Serpent}) {
    if (nameOf(shape) == name) {
      return shape;
    }
  }
  return std::nullopt;
}

std::optional<BodyPart> bodyPartNamed(std::string_view name) {
  for (const BodyPart part : {BodyPart::Neck, BodyPart::Head, BodyPart::Arm, BodyPart::Leg,
                              BodyPart::Tail, BodyPart::Wing, BodyPart::Other}) {
    if (nameOf(part) == name) {
      return part;
    }
  }
  return std::nullopt;
}

ChibiProportions defaultProportions(BodyShape shape) {
  switch (shape) {
  case BodyShape::Biped:
    return {.head = 1.6F,
            .torso = 0.8F,
            .neck = 0.7F,
            .arm = 0.7F,
            .leg = 0.65F,
            .hand = 1.2F,
            .foot = 1.15F,
            .tail = 0.7F,
            .wing = 0.8F,
            .girth = 1.1F};
  case BodyShape::Quadruped:
    return {.head = 1.5F,
            .torso = 0.75F,
            .neck = 0.6F,
            .arm = 0.7F,
            .leg = 0.65F,
            .hand = 1.15F,
            .foot = 1.15F,
            .tail = 0.65F,
            .wing = 0.8F,
            .girth = 1.1F};
  case BodyShape::Winged:
    return {.head = 1.5F,
            .torso = 0.85F,
            .neck = 0.7F,
            .arm = 0.7F,
            .leg = 0.7F,
            .hand = 1.1F,
            .foot = 1.15F,
            .tail = 0.7F,
            .wing = 0.8F,
            .girth = 1.1F};
  case BodyShape::Serpent:
    return {.head = 1.6F, .neck = 0.6F, .tail = 0.75F, .girth = 1.1F};
  case BodyShape::Unknown:
    break;
  }
  return {.head = 1.4F};
}

core::Result<ChibiOverrides> parseChibiOverrides(std::string_view yaml) {
  auto root = core::parseYaml(yaml);
  if (!root) {
    return std::unexpected{root.error()};
  }
  ChibiOverrides overrides;
  if (!root->isMapping()) {
    return failAt(*root, "the chibi overrides must be a mapping with 'species'");
  }
  for (const core::YamlEntry& entry : root->entries()) {
    if (entry.key != "species") {
      return failAt(entry.value, std::format("unknown key '{}'", entry.key));
    }
    if (!entry.value.isMapping()) {
      return failAt(entry.value, "'species' must map model folders to overrides");
    }
    for (const core::YamlEntry& species : entry.value.entries()) {
      auto override = readOverride(species.value);
      if (!override) {
        return std::unexpected{override.error()};
      }
      overrides[species.key] = std::move(*override);
    }
  }
  return overrides;
}

ChibiClassification classifyChibi(const ModelData& model, const ChibiOverride* override) {
  if (!model.skin || model.skin->joints.empty()) {
    return {};
  }
  return detect(model, override).classification;
}

namespace {

// Fills the rig's nodes and meshes from its classification and proportions.
void shapeRig(const ModelData& model, const Detected& found, ChibiRig& rig) {
  const assets::ModelSkin& skin = *model.skin;
  const Skeleton& skeleton = found.skeleton;
  const ChibiClassification& shape = rig.classification;
  const ChibiProportions& p = rig.proportions;
  const std::vector<BodyPart>& parts = shape.parts;
  // The torso's axis: up for an upright body, forward for one on four legs; a serpent's body is
  // its tail.
  std::optional<glm::vec3> torsoAxis;
  if (shape.shape == BodyShape::Quadruped) {
    torsoAxis = glm::vec3{0.0F, 0.0F, 1.0F};
  } else if (shape.shape != BodyShape::Serpent) {
    torsoAxis = glm::vec3{0.0F, 1.0F, 0.0F};
  }
  rig.nodes.assign(model.nodes.size(), {});
  rig.mesh.assign(skin.joints.size(), glm::mat4{1.0F});
  for (const std::uint32_t joint : skeleton.order) {
    const Bone& bone = skeleton.bones[joint];
    ChibiRig::Node& node = rig.nodes[skin.joints[joint]];
    const BodyPart part = parts[joint];
    if (bone.parent && found.trunk[*bone.parent] && torsoAxis) {
      // Hangs from the trunk: closer to it along its axis.
      node.translation = axisScale(inJointSpace(skin, *bone.parent, *torsoAxis), p.torso, 1.0F);
    } else if (bone.parent && skeleton.bones[*bone.parent].verts > 0 &&
               continues(parts[*bone.parent], part) && parts[*bone.parent] != BodyPart::Head) {
      // The next segment of a limb, neck or tail: the one before it is shorter.
      node.translation = glm::mat3{factorOf(p, parts[*bone.parent])};
    }
    if (shape.head == joint) {
      node.scale = p.head;
    }
    if (bone.verts == 0) {
      continue;
    }
    if (part == BodyPart::Trunk) {
      if (torsoAxis) {
        rig.mesh[joint] =
            glm::mat4{axisScale(inJointSpace(skin, joint, *torsoAxis), p.torso, 1.0F)};
      }
      continue;
    }
    if (part != BodyPart::Arm && part != BodyPart::Leg && part != BodyPart::Neck &&
        part != BodyPart::Tail && part != BodyPart::Wing) {
      continue;
    }
    const bool extremity = !hasChildIn(skeleton, joint, part, parts);
    if (extremity && (part == BodyPart::Arm || part == BodyPart::Leg)) {
      rig.mesh[joint] = glm::mat4{glm::mat3{part == BodyPart::Arm ? p.hand : p.foot}};
      continue;
    }
    if (const auto direction = segmentDirection(skeleton, joint, part, parts)) {
      rig.mesh[joint] =
          glm::mat4{axisScale(inJointSpace(skin, joint, *direction), factorOf(p, part), p.girth)};
    }
  }
}

// The lowest skinned vertex and its joint.
std::pair<float, std::uint16_t> lowestVertex(const ModelData& model,
                                             std::span<const glm::mat4> joints) {
  std::pair<float, std::uint16_t> lowest{Infinity, 0};
  for (const assets::ModelPrimitive& primitive : model.primitives) {
    for (const assets::ModelVertex& vertex : primitive.vertices) {
      const std::uint16_t joint = vertex.joints[0];
      if (joint < joints.size()) {
        const float y = (joints[joint] * glm::vec4{vertex.position, 1.0F}).y;
        if (y < lowest.first) {
          lowest = {y, joint};
        }
      }
    }
  }
  return lowest;
}

// Shrinks a part a little, so that it reaches less far; false if that part cannot shrink.
bool shrinkPart(ChibiProportions& p, BodyPart part) {
  constexpr float Step = 0.9F;
  switch (part) {
  case BodyPart::Arm:
    p.arm *= Step;
    p.hand = std::max(1.0F, p.hand * Step);
    return true;
  case BodyPart::Tail:
    p.tail *= Step;
    return true;
  case BodyPart::Wing:
    p.wing *= Step;
    return true;
  case BodyPart::Neck:
    p.neck *= Step;
    return true;
  case BodyPart::Head:
    p.head = std::max(1.0F, p.head * Step);
    return p.head > 1.0F;
  default:
    return false;
  }
}

} // namespace

ChibiRig buildChibiRig(const ModelData& model, const ChibiOverride* override) {
  ChibiRig rig;
  rig.nodes.resize(model.nodes.size());
  // Fewer than a body root and two bones under it: nothing to reshape.
  constexpr std::size_t FewestJoints = 3;
  if (!model.skin || model.skin->joints.size() < FewestJoints ||
      (override != nullptr && override->off)) {
    return rig;
  }
  const assets::ModelSkin& skin = *model.skin;
  const Detected found = detect(model, override);
  const Skeleton& skeleton = found.skeleton;
  if (skeleton.totalVerts == 0) {
    return rig;
  }
  rig.classification = found.classification;
  const ChibiClassification& shape = rig.classification;
  rig.proportions = defaultProportions(shape.shape);
  if (override != nullptr) {
    for (const auto& [name, factor] : override->scale) {
      if (float* proportion = proportionNamed(rig.proportions, name)) {
        *proportion = factor;
      }
    }
  }
  shapeRig(model, found, rig);
  rig.active = true;

  // The model's origin is the floor. One whose lowest point is well above it flies: it keeps the
  // height of its lowest point in the rest pose. One that stands keeps its feet (the legs' lowest
  // point, or the whole body's without legs) on the floor on every frame.
  const bool flies = skeleton.ground > GroundShare * skeleton.height;
  std::vector<NodeTransform> rest(model.nodes.size());
  restPose(model, rest);
  std::vector<glm::mat4> world(model.nodes.size());
  std::vector<glm::mat4> original(skin.joints.size());
  std::vector<glm::mat4> chibi(skin.joints.size());
  computeJointMatrices(model, rest, world, original);
  const float originalLowest = lowestVertex(model, original).first;
  if (flies) {
    rig.lift = 0.0F;
    computeChibiJointMatrices(model, rig, rest, original, world, chibi);
    rig.lift = originalLowest - lowestVertex(model, chibi).first;
    return rig;
  }
  const std::vector<BodyPart>& parts = shape.parts;
  for (const assets::ModelPrimitive& primitive : model.primitives) {
    for (const assets::ModelVertex& vertex : primitive.vertices) {
      const std::uint16_t joint = vertex.joints[0];
      if (joint < parts.size() && (shape.legs == 0 || parts[joint] == BodyPart::Leg)) {
        rig.ground.push_back({.joint = joint, .position = vertex.position});
      }
    }
  }
  // The mesh repeats a corner for each face's texture coordinates; one is enough.
  const auto key = [](const ChibiRig::GroundPoint& point) {
    return std::tuple{point.joint, point.position.x, point.position.y, point.position.z};
  };
  std::ranges::sort(rig.ground, {}, key);
  const auto repeated = std::ranges::unique(rig.ground, {}, key);
  rig.ground.erase(repeated.begin(), repeated.end());
  // With the feet on the floor, nothing else goes through it in the rest pose: a long arm (or a
  // tail, a wing, a drooping head) that would is shortened a step at a time.
  constexpr int Rounds = 8;
  constexpr float Slack = 1.0F;
  for (int round = 0; round < Rounds; ++round) {
    computeChibiJointMatrices(model, rig, rest, original, world, chibi);
    const auto [lowest, joint] = lowestVertex(model, chibi);
    if (lowest >= originalLowest - Slack || !shrinkPart(rig.proportions, parts[joint])) {
      break;
    }
    shapeRig(model, found, rig);
  }
  return rig;
}

void computeChibiJointMatrices(const ModelData& model, const ChibiRig& rig,
                               std::span<const NodeTransform> pose,
                               std::span<const glm::mat4> original, std::span<glm::mat4> nodeWorld,
                               std::span<glm::mat4> out) {
  DWE_ASSERT(original.size() == out.size());
  if (!rig.active || !model.skin) {
    std::ranges::copy(original, out.begin());
    return;
  }
  DWE_ASSERT(pose.size() == model.nodes.size() && nodeWorld.size() == model.nodes.size() &&
             rig.nodes.size() == model.nodes.size() && out.size() == jointCount(model) &&
             rig.mesh.size() == out.size());
  for (const std::uint32_t n : model.nodeOrder) {
    const ChibiRig::Node& node = rig.nodes[n];
    const NodeTransform& at = pose[n];
    const glm::vec3 translation = node.translation * at.translation;
    const glm::mat4 local = glm::translate(glm::mat4{1.0F}, translation) *
                            glm::mat4_cast(at.rotation) *
                            glm::scale(glm::mat4{1.0F}, at.scale * node.scale);
    const auto& parent = model.nodes[n].parent;
    nodeWorld[n] = parent ? nodeWorld[*parent] * local : local;
  }
  for (std::size_t j = 0; j < out.size(); ++j) {
    out[j] = nodeWorld[model.skin->joints[j]] * rig.mesh[j] * model.skin->inverseBindMatrices[j];
  }
  if (rig.lift) {
    const glm::mat4 lift = glm::translate(glm::mat4{1.0F}, glm::vec3{0.0F, *rig.lift, 0.0F});
    for (glm::mat4& joint : out) {
      joint = lift * joint;
    }
    return;
  }
  // On every frame the lowest point of what stands on the ground is as high as the original's: the
  // feet touch the floor (and leave it in a jump) as they do.
  float lowest = Infinity;
  float lowestOriginal = Infinity;
  // Only the height of each point: the matrices' second row.
  const auto heightOf = [](const glm::mat4& joint, const glm::vec3& at) {
    return (joint[0][1] * at.x) + (joint[1][1] * at.y) + (joint[2][1] * at.z) + joint[3][1];
  };
  for (const ChibiRig::GroundPoint& point : rig.ground) {
    lowest = std::min(lowest, heightOf(out[point.joint], point.position));
    lowestOriginal = std::min(lowestOriginal, heightOf(original[point.joint], point.position));
  }
  if (lowest != Infinity) {
    const glm::mat4 lift =
        glm::translate(glm::mat4{1.0F}, glm::vec3{0.0F, lowestOriginal - lowest, 0.0F});
    for (glm::mat4& joint : out) {
      joint = lift * joint;
    }
  }
}

float lowestPoint(const ModelData& model, std::span<const glm::mat4> joints) {
  float lowest = Infinity;
  for (const assets::ModelPrimitive& primitive : model.primitives) {
    for (const assets::ModelVertex& vertex : primitive.vertices) {
      const std::size_t joint = model.skin ? vertex.joints[0] : 0;
      if (joint < joints.size()) {
        lowest = std::min(lowest, (joints[joint] * glm::vec4{vertex.position, 1.0F}).y);
      }
    }
  }
  return lowest == Infinity ? 0.0F : lowest;
}

} // namespace dwe::animation
