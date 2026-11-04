#pragma once

#include <dwe/assets/model_data.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The chibi style (an engine improvement; the original has none): the original game's models are
// already chibi (a big head, a short body, short limbs), and they are the reference. A model from
// elsewhere is measured against the original's proportions for its body shape (the profile,
// models/chibi_profile.yaml in the game pack, measured from every original model): one already
// within the original's range is left as it is; one outside it is drawn with its head, limbs, hands
// and feet scaled toward the original's, its feet still on the ground. It is a change of the
// drawing only: the animations run unchanged and the skinning matrices are rebuilt from their pose,
// so collisions, speeds and every size the game logic reads stay the model's own.
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

// What the style measures of a model, in its rest pose. A head's size is the largest side of its
// box; a limb's (arm, leg, neck, tail, wing) reach is how far its farthest vertex is from the joint
// it hangs from; a hand's or foot's size how big the end of the arm or leg (its last 30%) is across
// it, whatever bones it is made of;
// the torso's length the trunk's extent along its axis (up for an upright body, forward for one on
// four legs); a width the extent across (x).
enum class BodyRatio : std::uint8_t {
  // Over the model's height (they tell whether a model is chibi; the factors follow the others):
  // the head's size and the torso's length (not for a serpent, whose body is its tail).
  Head,
  Torso,
  // The head's size over the torso's length, and its width over the trunk's (its shoulders).
  HeadSize,
  HeadWidth,
  // Reaches over the torso's length.
  Arm,
  Leg,
  Neck,
  Tail,
  Wing,
  // Sizes over the torso's length.
  Hand,
  Foot,
};
inline constexpr std::size_t BodyRatioCount = 11;
inline constexpr std::array<BodyRatio, BodyRatioCount> BodyRatios{
    BodyRatio::Head, BodyRatio::Torso, BodyRatio::HeadSize, BodyRatio::HeadWidth,
    BodyRatio::Arm,  BodyRatio::Leg,   BodyRatio::Neck,     BodyRatio::Tail,
    BodyRatio::Wing, BodyRatio::Hand,  BodyRatio::Foot};

[[nodiscard]] std::string_view nameOf(BodyRatio ratio);
[[nodiscard]] std::optional<BodyRatio> bodyRatioNamed(std::string_view name);

// A model's proportions; a ratio it has no part for (no arms, no tail...) is missing.
struct BodyMeasure {
  std::array<std::optional<float>, BodyRatioCount> ratios{};
  float height = 0.0F;
  float torso = 0.0F;

  [[nodiscard]] std::optional<float> operator[](BodyRatio ratio) const {
    return ratios[static_cast<std::size_t>(ratio)];
  }
};

// The original's range of one ratio over the models of one body shape: from the lowest to the
// highest (every original model is chibi) and the middle (the median).
struct ChibiRange {
  float low = 0.0F;
  float median = 0.0F;
  float high = 0.0F;
  std::uint32_t samples = 0;

  // Within the range, give or take the rounding of the written profile (a thousandth).
  [[nodiscard]] bool contains(float value) const {
    constexpr float Rounding = 0.001F;
    return value >= low * (1.0F - Rounding) && value <= high * (1.0F + Rounding);
  }
};

// The original's proportions per body shape (models/chibi_profile.yaml).
struct ChibiProfile {
  struct Shape {
    std::uint32_t models = 0;
    std::array<std::optional<ChibiRange>, BodyRatioCount> ranges{};

    [[nodiscard]] const std::optional<ChibiRange>& operator[](BodyRatio ratio) const {
      return ranges[static_cast<std::size_t>(ratio)];
    }
  };
  std::map<BodyShape, Shape> shapes;

  [[nodiscard]] const Shape* find(BodyShape shape) const {
    const auto found = shapes.find(shape);
    return found == shapes.end() ? nullptr : &found->second;
  }
};

// The profile of a set of measured models (each with its shape); a ratio needs this many models
// that have it to get a range.
inline constexpr std::uint32_t FewestProfileSamples = 3;
[[nodiscard]] ChibiProfile profileOf(std::span<const std::pair<BodyShape, BodyMeasure>> models);

// Reads and writes models/chibi_profile.yaml: `shapes: {<shape>: {models: n, <ratio>: {low, median,
// high, samples}}}`.
[[nodiscard]] core::Result<ChibiProfile> parseChibiProfile(std::string_view yaml);
[[nodiscard]] std::string writeChibiProfile(const ChibiProfile& profile);

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
  // The model's own proportions, the profile's range they were compared with (none for a shape the
  // profile does not have) and which ratios fell outside it.
  BodyMeasure measure;
  ChibiProfile::Shape reference;
  std::array<bool, BodyRatioCount> outside{};
  // Already chibi: every measured ratio within the original's range (the rig is inactive).
  bool alreadyChibi = false;
  // The proportions once drawn chibi.
  BodyMeasure drawn;
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
  // False for a model the style leaves as it is (no skin, too few bones, a shape the detection
  // does not know, or turned off).
  bool active = false;
};

// Measures a model whose parts are `parts`, posed with these skinning matrices (its rest pose's,
// or the chibi style's).
[[nodiscard]] BodyMeasure measureBody(const assets::ModelData& model,
                                      const ChibiClassification& parts,
                                      std::span<const glm::mat4> joints);

// The rest pose's skinning matrices of a model.
[[nodiscard]] std::vector<glm::mat4> restJoints(const assets::ModelData& model);

// Builds the rig of a model. The detector measures it: a model whose every ratio is within the
// profile's range for its shape (already chibi) gets an inactive rig, drawn exactly as it is; any
// other gets factors for its head, limbs, hands and feet (the torso is the unit and stays) that
// bring each ratio into the range, and toward the original's middle as far as the model was out
// of it (fully from twice or half an end on), each factor between ChibiMinFactor and
// ChibiMaxFactor (a neck only shrinks, a hand or foot grows up to ChibiMaxExtremity). A species'
// override can turn it off, fix its parts or give factors of its own.
inline constexpr float ChibiMinFactor = 0.35F;
inline constexpr float ChibiMaxFactor = 3.0F;
inline constexpr float ChibiMaxExtremity = 2.0F;
[[nodiscard]] ChibiRig buildChibiRig(const assets::ModelData& model, const ChibiProfile& profile,
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
