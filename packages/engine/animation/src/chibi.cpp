#include <dwe/animation/chibi.hpp>
#include <dwe/animation/clip_sampling.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/number_text.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
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
    const auto [end, error] = core::fromChars(text.data(), text.data() + text.size(), value);
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

glm::vec3 skinned(std::span<const glm::mat4> joints, std::uint32_t joint, glm::vec3 at) {
  return glm::vec3{joints[joint] * glm::vec4{at, 1.0F}};
}

struct Box {
  glm::vec3 low{Infinity};
  glm::vec3 high{-Infinity};

  void add(glm::vec3 at) {
    low = glm::min(low, at);
    high = glm::max(high, at);
  }
  [[nodiscard]] bool empty() const { return low.x > high.x; }
  [[nodiscard]] glm::vec3 size() const { return empty() ? glm::vec3{0.0F} : high - low; }
  [[nodiscard]] float largest() const {
    const glm::vec3 side = size();
    return std::max({side.x, side.y, side.z});
  }
};

bool isLimbPart(BodyPart part) {
  return part == BodyPart::Arm || part == BodyPart::Leg || part == BodyPart::Neck ||
         part == BodyPart::Tail || part == BodyPart::Wing;
}

std::optional<BodyRatio> reachRatio(BodyPart part) {
  switch (part) {
  case BodyPart::Arm:
    return BodyRatio::Arm;
  case BodyPart::Leg:
    return BodyRatio::Leg;
  case BodyPart::Neck:
    return BodyRatio::Neck;
  case BodyPart::Tail:
    return BodyRatio::Tail;
  case BodyPart::Wing:
    return BodyRatio::Wing;
  default:
    return std::nullopt;
  }
}

BodyMeasure measure(const ModelData& model, const Skeleton& skeleton,
                    const ChibiClassification& shape, std::span<const glm::mat4> joints) {
  BodyMeasure out;
  const std::vector<BodyPart>& parts = shape.parts;
  const std::size_t count = skeleton.bones.size();
  if (parts.size() != count || joints.size() < count) {
    return out;
  }
  // Each limb's first bone, for every bone of it.
  std::vector<std::uint32_t> limbRoot(count);
  for (const std::uint32_t joint : skeleton.order) {
    const auto& parent = skeleton.bones[joint].parent;
    limbRoot[joint] = parent && parts[*parent] == parts[joint] ? limbRoot[*parent] : joint;
  }
  std::vector<glm::vec3> rootAt(count);
  for (std::uint32_t joint = 0; joint < count; ++joint) {
    rootAt[joint] = skinned(joints, joint, skeleton.bones[joint].joint);
  }
  Box all;
  Box trunk;
  Box body;
  Box head;
  // Per limb root: how far its farthest vertex is, and where.
  std::vector<float> reach(count, 0.0F);
  std::vector<glm::vec3> farthest(count);
  for (const assets::ModelPrimitive& primitive : model.primitives) {
    for (const assets::ModelVertex& vertex : primitive.vertices) {
      const std::uint16_t joint = vertex.joints[0];
      if (joint >= count) {
        continue;
      }
      const glm::vec3 at = skinned(joints, joint, vertex.position);
      all.add(at);
      const BodyPart part = parts[joint];
      if (part == BodyPart::Trunk) {
        trunk.add(at);
      } else if (part == BodyPart::Head) {
        head.add(at);
      } else if (isLimbPart(part)) {
        if (part == BodyPart::Tail) {
          // A serpent's body is its tail (its trunk often has no mesh of its own).
          body.add(at);
        }
        const std::uint32_t root = limbRoot[joint];
        const float far = glm::length(at - rootAt[root]);
        if (far > reach[root]) {
          reach[root] = far;
          farthest[root] = at;
        }
      }
    }
  }
  // A hand's or foot's size: how big the end of the arm or leg is (its vertices past EndShare of
  // its reach) across the limb, whatever bones it is made of.
  constexpr float EndShare = 0.7F;
  std::vector<glm::vec2> endLow(count, glm::vec2{Infinity});
  std::vector<glm::vec2> endHigh(count, glm::vec2{-Infinity});
  std::vector<glm::mat3> across(count, glm::mat3{1.0F});
  for (std::uint32_t joint = 0; joint < count; ++joint) {
    if (reach[joint] > 0.0F) {
      const glm::vec3 along = glm::normalize(farthest[joint] - rootAt[joint]);
      const glm::vec3 side = std::abs(along.y) < 0.9F ? glm::vec3{0, 1, 0} : glm::vec3{1, 0, 0};
      const glm::vec3 u = glm::normalize(glm::cross(along, side));
      across[joint] = glm::transpose(glm::mat3{u, glm::cross(along, u), along});
    }
  }
  for (const assets::ModelPrimitive& primitive : model.primitives) {
    for (const assets::ModelVertex& vertex : primitive.vertices) {
      const std::uint16_t joint = vertex.joints[0];
      if (joint >= count || (parts[joint] != BodyPart::Arm && parts[joint] != BodyPart::Leg)) {
        continue;
      }
      const std::uint32_t root = limbRoot[joint];
      const glm::vec3 at = skinned(joints, joint, vertex.position) - rootAt[root];
      if (glm::length(at) >= EndShare * reach[root]) {
        const glm::vec2 flat{across[root] * at};
        endLow[root] = glm::min(endLow[root], flat);
        endHigh[root] = glm::max(endHigh[root], flat);
      }
    }
  }
  if (trunk.empty() && shape.shape == BodyShape::Serpent) {
    trunk = body;
  }
  if (all.empty() || trunk.empty()) {
    return out;
  }
  out.height = std::max(all.size().y, 1.0F);
  const glm::vec3 trunkSize = trunk.size();
  out.torso = std::max(shape.shape == BodyShape::Quadruped ? trunkSize.z
                       : shape.shape == BodyShape::Serpent ? trunk.largest()
                                                           : trunkSize.y,
                       1.0F);
  const auto set = [&](BodyRatio ratio, float value) {
    out.ratios[static_cast<std::size_t>(ratio)] = value;
  };
  if (!head.empty()) {
    set(BodyRatio::Head, head.largest() / out.height);
    set(BodyRatio::HeadSize, head.largest() / out.torso);
    set(BodyRatio::HeadWidth, head.size().x / std::max(trunkSize.x, 1.0F));
  }
  if (shape.shape != BodyShape::Serpent) {
    set(BodyRatio::Torso, out.torso / out.height);
  }
  // The mean over the limbs (or hands, feet) of each kind.
  std::array<float, BodyRatioCount> sum{};
  std::array<int, BodyRatioCount> seen{};
  const auto add = [&](BodyRatio ratio, float value) {
    sum[static_cast<std::size_t>(ratio)] += value;
    ++seen[static_cast<std::size_t>(ratio)];
  };
  for (std::uint32_t joint = 0; joint < count; ++joint) {
    const BodyPart part = parts[joint];
    if (!isLimbPart(part)) {
      continue;
    }
    if (limbRoot[joint] != joint || reach[joint] <= 0.0F) {
      continue;
    }
    add(*reachRatio(part), reach[joint]);
    if ((part == BodyPart::Arm || part == BodyPart::Leg) && endLow[joint].x <= endHigh[joint].x) {
      const glm::vec2 size = endHigh[joint] - endLow[joint];
      add(part == BodyPart::Arm ? BodyRatio::Hand : BodyRatio::Foot, std::max(size.x, size.y));
    }
  }
  for (const BodyRatio ratio : BodyRatios) {
    const auto index = static_cast<std::size_t>(ratio);
    if (seen[index] > 0) {
      set(ratio, sum[index] / static_cast<float>(seen[index]) / out.torso);
    }
  }
  return out;
}

std::vector<glm::mat4> restMatrices(const ModelData& model) {
  std::vector<NodeTransform> rest(model.nodes.size());
  restPose(model, rest);
  std::vector<glm::mat4> world(model.nodes.size());
  std::vector<glm::mat4> joints(jointCount(model));
  computeJointMatrices(model, rest, world, joints);
  return joints;
}

// Whether a factor follows the ratio: the ones against the torso. The ones against the height only
// tell whether the model is chibi.
bool drives(BodyRatio ratio) {
  return ratio != BodyRatio::Head && ratio != BodyRatio::Torso;
}

float* factorFor(ChibiProportions& p, BodyRatio ratio) {
  switch (ratio) {
  case BodyRatio::Head:
  case BodyRatio::Torso:
    return nullptr;
  case BodyRatio::HeadSize:
  case BodyRatio::HeadWidth:
    return &p.head;
  case BodyRatio::Arm:
    return &p.arm;
  case BodyRatio::Leg:
    return &p.leg;
  case BodyRatio::Neck:
    return &p.neck;
  case BodyRatio::Tail:
    return &p.tail;
  case BodyRatio::Wing:
    return &p.wing;
  case BodyRatio::Hand:
    return &p.hand;
  case BodyRatio::Foot:
    return &p.foot;
  }
  return nullptr;
}

float quantile(const std::vector<float>& sorted, float share) {
  const float at = share * static_cast<float>(sorted.size() - 1);
  const auto below = static_cast<std::size_t>(std::floor(at));
  const std::size_t above = std::min(below + 1, sorted.size() - 1);
  const float t = at - static_cast<float>(below);
  return (sorted[below] * (1.0F - t)) + (sorted[above] * t);
}

core::Result<float> readNumber(const core::YamlNode& node) {
  float value = 0.0F;
  if (node.isScalar()) {
    const std::string& text = node.text();
    const auto [end, error] = core::fromChars(text.data(), text.data() + text.size(), value);
    if (error == std::errc{} && end == text.data() + text.size() && value >= 0.0F) {
      return value;
    }
  }
  return failAt(node, "a profile value must be a number of zero or more");
}

// Shrinks a part a little, so that it reaches less far; false if that part cannot shrink.
bool shrinkPart(ChibiProportions& p, BodyPart part) {
  constexpr float Step = 0.9F;
  switch (part) {
  case BodyPart::Arm:
    p.arm = std::max(ChibiMinFactor, p.arm * Step);
    return p.arm > ChibiMinFactor;
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

std::string_view nameOf(BodyRatio ratio) {
  switch (ratio) {
  case BodyRatio::Head:
    return "head";
  case BodyRatio::HeadSize:
    return "head_size";
  case BodyRatio::HeadWidth:
    return "head_width";
  case BodyRatio::Torso:
    return "torso";
  case BodyRatio::Arm:
    return "arm";
  case BodyRatio::Leg:
    return "leg";
  case BodyRatio::Neck:
    return "neck";
  case BodyRatio::Tail:
    return "tail";
  case BodyRatio::Wing:
    return "wing";
  case BodyRatio::Hand:
    return "hand";
  case BodyRatio::Foot:
    return "foot";
  }
  return "head";
}

std::optional<BodyRatio> bodyRatioNamed(std::string_view name) {
  const auto found = std::ranges::find(BodyRatios, name, [](BodyRatio r) { return nameOf(r); });
  return found == BodyRatios.end() ? std::nullopt : std::optional{*found};
}

BodyMeasure measureBody(const ModelData& model, const ChibiClassification& parts,
                        std::span<const glm::mat4> joints) {
  if (!model.skin || model.skin->joints.empty()) {
    return {};
  }
  return measure(model, readSkeleton(model), parts, joints);
}

std::vector<glm::mat4> restJoints(const ModelData& model) {
  return restMatrices(model);
}

ChibiProfile profileOf(std::span<const std::pair<BodyShape, BodyMeasure>> models) {
  std::map<BodyShape, std::array<std::vector<float>, BodyRatioCount>> values;
  ChibiProfile profile;
  for (const auto& [shape, measured] : models) {
    if (shape == BodyShape::Unknown) {
      continue;
    }
    ++profile.shapes[shape].models;
    for (std::size_t r = 0; r < BodyRatioCount; ++r) {
      if (measured.ratios[r]) {
        values[shape][r].push_back(*measured.ratios[r]);
      }
    }
  }
  for (auto& [shape, ratios] : values) {
    for (std::size_t r = 0; r < BodyRatioCount; ++r) {
      std::vector<float>& sorted = ratios[r];
      if (sorted.size() < FewestProfileSamples) {
        continue;
      }
      std::ranges::sort(sorted);
      profile.shapes[shape].ranges[r] =
          ChibiRange{.low = sorted.front(),
                     .median = quantile(sorted, 0.5F),
                     .high = sorted.back(),
                     .samples = static_cast<std::uint32_t>(sorted.size())};
    }
  }
  return profile;
}

core::Result<ChibiProfile> parseChibiProfile(std::string_view yaml) {
  auto root = core::parseYaml(yaml);
  if (!root) {
    return std::unexpected{root.error()};
  }
  const core::YamlNode* shapes = root->isMapping() ? root->find("shapes") : nullptr;
  if (shapes == nullptr || !shapes->isMapping()) {
    return failAt(*root, "the chibi profile must be a mapping with 'shapes'");
  }
  ChibiProfile profile;
  for (const core::YamlEntry& entry : shapes->entries()) {
    const auto shape = bodyShapeNamed(entry.key);
    if (!shape || *shape == BodyShape::Unknown || !entry.value.isMapping()) {
      return failAt(entry.value, std::format("'{}' is not a body shape with ratios", entry.key));
    }
    ChibiProfile::Shape& read = profile.shapes[*shape];
    for (const core::YamlEntry& field : entry.value.entries()) {
      if (field.key == "models") {
        auto models = readNumber(field.value);
        if (!models) {
          return std::unexpected{models.error()};
        }
        read.models = static_cast<std::uint32_t>(*models);
        continue;
      }
      const auto ratio = bodyRatioNamed(field.key);
      if (!ratio || !field.value.isMapping()) {
        return failAt(field.value, std::format("unknown ratio '{}'", field.key));
      }
      ChibiRange range;
      for (const auto& [name, value] : std::array<std::pair<std::string_view, float*>, 3>{
               {{"low", &range.low}, {"median", &range.median}, {"high", &range.high}}}) {
        const core::YamlNode* node = field.value.find(name);
        if (node == nullptr) {
          return failAt(field.value, std::format("'{}' needs '{}'", field.key, name));
        }
        auto number = readNumber(*node);
        if (!number) {
          return std::unexpected{number.error()};
        }
        *value = *number;
      }
      if (const core::YamlNode* samples = field.value.find("samples")) {
        auto number = readNumber(*samples);
        if (!number) {
          return std::unexpected{number.error()};
        }
        range.samples = static_cast<std::uint32_t>(*number);
      }
      if (range.low <= 0.0F || range.low > range.median || range.median > range.high) {
        return failAt(field.value,
                      std::format("'{}' must have 0 < low <= median <= high", field.key));
      }
      read.ranges[static_cast<std::size_t>(*ratio)] = range;
    }
  }
  return profile;
}

std::string writeChibiProfile(const ChibiProfile& profile) {
  std::string out = "shapes:\n";
  for (const auto& [shape, read] : profile.shapes) {
    out += std::format("  {}:\n    models: {}\n", nameOf(shape), read.models);
    for (const BodyRatio ratio : BodyRatios) {
      if (const auto& range = read[ratio]) {
        out += std::format("    {}: {{low: {:.4f}, median: {:.4f}, high: {:.4f}, samples: {}}}\n",
                           nameOf(ratio), range->low, range->median, range->high, range->samples);
      }
    }
  }
  return out;
}

ChibiRig buildChibiRig(const ModelData& model, const ChibiProfile& profile,
                       const ChibiOverride* override) {
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
  // A body the detection does not understand is left as it is (an override can give its shape).
  if (shape.shape == BodyShape::Unknown) {
    return rig;
  }
  std::vector<glm::mat4> original = restMatrices(model);
  rig.measure = measure(model, skeleton, shape, original);
  rig.drawn = rig.measure;
  const ChibiProfile::Shape* reference = profile.find(shape.shape);
  if (reference == nullptr) {
    return rig;
  }
  rig.reference = *reference;
  // The detector: the ratios the model and the original's profile both have, against its range;
  // how far out the model is, by its farthest ratio (0 within the range, 1 from twice or half an
  // end of it on).
  bool compared = false;
  float out = 0.0F;
  for (const BodyRatio ratio : BodyRatios) {
    const auto r = static_cast<std::size_t>(ratio);
    const auto& range = rig.reference.ranges[r];
    const auto own = rig.measure.ratios[r];
    if (!range || !own || *own <= 0.0F) {
      continue;
    }
    compared = true;
    if (!range->contains(*own)) {
      rig.outside[r] = true;
      const float end = *own < range->low ? range->low : range->high;
      out = std::max(out, std::min(1.0F, std::abs(std::log(*own / end)) / std::log(2.0F)));
    }
  }
  rig.alreadyChibi = compared && std::ranges::none_of(rig.outside, std::identity{});
  const bool given = override != nullptr && !override->scale.empty();
  if (!compared || (rig.alreadyChibi && !given)) {
    return rig;
  }
  rig.active = true;
  // Where each ratio a factor drives goes: into the range (just inside its nearer end), and from
  // there toward the original's middle as far as the model was out, so that one just outside
  // changes only a little and one far outside lands on the original's typical proportions.
  std::array<std::optional<float>, BodyRatioCount> target{};
  constexpr float Inside = 0.01F;
  for (const BodyRatio ratio : BodyRatios) {
    const auto r = static_cast<std::size_t>(ratio);
    const auto& range = rig.reference.ranges[r];
    const auto own = rig.measure.ratios[r];
    if (!range || !own || *own <= 0.0F || !drives(ratio)) {
      continue;
    }
    const float low = std::min(range->low * (1.0F + Inside), range->median);
    const float high = std::max(range->high * (1.0F - Inside), range->median);
    const float start = range->contains(*own) ? *own : std::clamp(*own, low, high);
    target[r] = start * std::pow(range->median / start, out);
  }

  // The factors, a few rounds: each scales its part by how far the drawn ratio is from its target
  // (the torso is the unit: the head, limbs, hands and feet are measured against it, so they do
  // not pull on each other); a head's size and width move the head together. An override's
  // factors stay as given.
  std::vector<NodeTransform> rest(model.nodes.size());
  restPose(model, rest);
  std::vector<glm::mat4> world(model.nodes.size());
  std::vector<glm::mat4> chibi(skin.joints.size());
  const auto fixed = [&](float* factor) {
    if (override == nullptr) {
      return false;
    }
    return std::ranges::any_of(override->scale, [&](const auto& entry) {
      return proportionNamed(rig.proportions, entry.first) == factor;
    });
  };
  if (override != nullptr) {
    for (const auto& [name, factor] : override->scale) {
      if (float* proportion = proportionNamed(rig.proportions, name)) {
        *proportion = factor;
      }
    }
  }
  constexpr int FitRounds = 30;
  constexpr float Settled = 0.002F;
  for (int round = 0; round < FitRounds; ++round) {
    shapeRig(model, found, rig);
    computeChibiJointMatrices(model, rig, rest, original, world, chibi);
    rig.drawn = measure(model, skeleton, shape, chibi);
    std::map<float*, std::pair<float, int>> steps;
    for (const BodyRatio ratio : BodyRatios) {
      const auto r = static_cast<std::size_t>(ratio);
      const auto now = rig.drawn.ratios[r];
      float* factor = factorFor(rig.proportions, ratio);
      if (!target[r] || !now || *now <= 0.0F || factor == nullptr || fixed(factor)) {
        continue;
      }
      auto& [product, count] = steps[factor];
      product = count == 0 ? *target[r] / *now : product * (*target[r] / *now);
      ++count;
    }
    bool settled = true;
    for (auto& [factor, step] : steps) {
      const float by = std::pow(step.first, 1.0F / static_cast<float>(step.second));
      settled = settled && std::abs(by - 1.0F) < Settled;
      // A chibi has hardly any neck: one never grows; a hand or foot grows at most twice its size
      // (a box of a hand three times as big reads as a glove, not as the original's big hands).
      const float most = factor == &rig.proportions.neck ? 1.0F
                         : factor == &rig.proportions.hand || factor == &rig.proportions.foot
                             ? ChibiMaxExtremity
                             : ChibiMaxFactor;
      *factor = std::clamp(*factor * by, ChibiMinFactor, most);
    }
    if (settled) {
      break;
    }
  }
  shapeRig(model, found, rig);

  // The model's origin is the floor. One whose lowest point is well above it flies: it keeps the
  // height of its lowest point in the rest pose. One that stands keeps its feet (the legs' lowest
  // point, or the whole body's without legs) on the floor on every frame.
  const bool flies = skeleton.ground > GroundShare * skeleton.height;
  const float originalLowest = lowestVertex(model, original).first;
  if (flies) {
    rig.lift = 0.0F;
    computeChibiJointMatrices(model, rig, rest, original, world, chibi);
    rig.lift = originalLowest - lowestVertex(model, chibi).first;
    rig.drawn = measure(model, skeleton, shape, chibi);
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
  computeChibiJointMatrices(model, rig, rest, original, world, chibi);
  rig.drawn = measure(model, skeleton, shape, chibi);
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
