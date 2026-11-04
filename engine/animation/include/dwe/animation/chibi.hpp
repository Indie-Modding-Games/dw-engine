#pragma once

#include <dwe/assets/model_data.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The chibi style (an engine improvement, a prototype; the original has none): a model drawn with a
// bigger head, a shorter torso and short limbs, its feet still on the ground. It is a change of the
// drawing only: the animations run unchanged and the skinning matrices are rebuilt from their pose,
// so collisions, speeds and every size the game logic reads stay the original's.
namespace dwe::animation {

// What a model is built like, from its skeleton and its mesh (classifyChibi).
enum class BodyShape : std::uint8_t { Unknown, Biped, Quadruped, Winged, Serpent };

// What a bone is part of.
enum class BodyPart : std::uint8_t { None, Trunk, Neck, Head, Arm, Leg, Tail, Wing, Other };

[[nodiscard]] std::string_view nameOf(BodyShape shape);
[[nodiscard]] std::string_view nameOf(BodyPart part);
[[nodiscard]] std::optional<BodyShape> bodyShapeNamed(std::string_view name);
[[nodiscard]] std::optional<BodyPart> bodyPartNamed(std::string_view name);

// How much each part grows or shrinks: a head scales as a whole around its joint, a torso along its
// axis (up for an upright body, forward for one on four legs), a limb, neck or tail in length (its
// bones closer together, each segment squashed along itself and `girth` across), and the last
// segment of an arm or leg (hand, foot) as a whole.
struct ChibiProportions {
  float head = 1.0F;
  float torso = 1.0F;
  float neck = 1.0F;
  float arm = 1.0F;
  float leg = 1.0F;
  float hand = 1.0F;
  float foot = 1.0F;
  float tail = 1.0F;
  float wing = 1.0F;
  float girth = 1.0F;
};

// The proportions a body shape gets unless an override says otherwise.
[[nodiscard]] ChibiProportions defaultProportions(BodyShape shape);

// A species' corrections, for when the detection gets it wrong (models/chibi.yaml in the game
// pack).
struct ChibiOverride {
  // Off: the species keeps its original look.
  bool off = false;
  std::optional<BodyShape> shape;
  // The bone (joint index, the original's object order) drawn as the head.
  std::optional<std::uint32_t> head;
  // The part of the limb, tail or other appendage that hangs from this bone.
  std::map<std::uint32_t, BodyPart> parts;
  // Proportions that replace the shape's, by name (head, torso, neck, arm, leg, hand, foot, tail,
  // wing, girth).
  std::map<std::string, float, std::less<>> scale;
};

// The overrides of every species, by the folder of its model.
using ChibiOverrides = std::map<std::string, ChibiOverride, std::less<>>;

// Reads models/chibi.yaml: `species: {<folder>: {off, shape, head, parts: {bone: part}, scale:
// {part: factor}}}`. Factors go from 0.25 to 4.
[[nodiscard]] core::Result<ChibiOverrides> parseChibiOverrides(std::string_view yaml);

// What the detection found, per joint of the skin.
struct ChibiClassification {
  BodyShape shape = BodyShape::Unknown;
  std::vector<BodyPart> parts;
  // The joint drawn as the head (scaled with everything that hangs from it).
  std::optional<std::uint32_t> head;
  std::uint32_t legs = 0;
  std::uint32_t arms = 0;
  std::uint32_t wings = 0;
  std::uint32_t tails = 0;
};

// Finds the parts of a model from its skeleton and the mesh of each bone, in its rest pose:
// - the trunk is every bone from the body's root down to the last one where two limbs (two
//   branches with two or more bones with a mesh each) part; the branches that hang from it are
//   appendages;
// - a leg reaches the ground (its lowest vertex within a sixteenth of the height of the model's
//   lowest), hangs from the lower part of the body and goes down more than sideways;
// - the head is the branch, near the middle, that sits highest and most forward of the trunk;
// - the other branches with two or more bones are arms (to a side, hanging), wings (to a side, not
//   hanging, or going back) or tails (in the middle, behind); a branch of one bone is left as is;
// - four legs make a quadruped, none with a long tail a serpent, wings and no arms a winged body,
//   and two legs or two arms a biped.
[[nodiscard]] ChibiClassification classifyChibi(const assets::ModelData& model,
                                                const ChibiOverride* override = nullptr);

// How one model is drawn chibi: per node, the change to its translation and the scale it passes
// on; per joint, the scale of its own mesh; and the vertices that stand on the ground.
struct ChibiRig {
  struct Node {
    // Applied to the node's translation (in its parent's space).
    glm::mat3 translation{1.0F};
    // Scales the node and everything under it (the head).
    float scale = 1.0F;
  };
  struct GroundPoint {
    std::uint16_t joint = 0;
    glm::vec3 position{0.0F};
  };
  ChibiClassification classification;
  ChibiProportions proportions;
  std::vector<Node> nodes;
  // Per joint, in the joint's space.
  std::vector<glm::mat4> mesh;
  // The legs' vertices (the whole body's for one without legs), bind pose: each frame the model
  // moves up or down so that their lowest is as high as in the original's pose.
  std::vector<GroundPoint> ground;
  // A model that flies (its lowest point well above the floor) moves up or down by this instead,
  // which keeps its rest pose's lowest point where the original's is.
  std::optional<float> lift;
  // False for a model the style leaves as it is (no skin, too few bones, or turned off).
  bool active = false;
};

// Builds the rig of a model, with the species' override if any.
[[nodiscard]] ChibiRig buildChibiRig(const assets::ModelData& model,
                                     const ChibiOverride* override = nullptr);

// The skinning matrices of a pose drawn chibi. `original` are the pose's own (computeJointMatrices,
// AnimationPlayer::jointMatrices), which an inactive rig gives as they are; `nodeWorld` is scratch,
// one per node.
void computeChibiJointMatrices(const assets::ModelData& model, const ChibiRig& rig,
                               std::span<const assets::NodeTransform> pose,
                               std::span<const glm::mat4> original, std::span<glm::mat4> nodeWorld,
                               std::span<glm::mat4> out);

// The lowest model-space height of the model's vertices skinned with these matrices (for the
// ground and the tests).
[[nodiscard]] float lowestPoint(const assets::ModelData& model, std::span<const glm::mat4> joints);

} // namespace dwe::animation
