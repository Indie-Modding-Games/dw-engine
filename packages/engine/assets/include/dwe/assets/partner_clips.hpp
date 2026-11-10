#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The recipes that give a model the partner clips it lacks (the game pack's
// animations/partner_clips.yaml, content/schemas/partner_clips.schema.json): the character models
// have only the few clips their scenes play, and as the partner they need every field and battle
// clip. Nothing of the disc is in the recipes: the clips are made when the model loads, from the
// user's extracted models (packages/engine/animation: clip_synthesis.hpp).

// The shape of a made clip: where it moves the body, the head and the limbs over its length.
enum class MotionKind : std::uint8_t {
  Breathe, // the body rises and sinks, the head nods with it
  Stride,  // a step cycle: the legs and arms swing, the body bobs and rolls
  Bounce,  // hops with the arms up
  Shake,   // the body turns from side to side
  Nod,     // the body bends forward and back (eating)
  Lie,     // the body goes down onto its side and breathes there
  Squat,   // the body goes down on bent legs and shivers
  Droop,   // a slow sway, bent forward
  Guard,   // crouched, leaning back, the arms up in front
  Recoil,  // a blow throws the body back (forward from behind) and it recovers
  Fall,    // the body falls onto its side and stays
  Rise,    // from lying on its side back up
  Lunge,   // wind-up back, a blow forward on the strike, back again
};

// A clip's motion and its numbers. Angles are degrees, heights fractions of the hips' height.
struct MotionRecipe {
  MotionKind kind = MotionKind::Breathe;
  // How many cycles (steps, hops, shakes, nods) the clip has.
  std::int32_t beats = 1;
  float angle = 0.0F;
  float height = 0.0F;
  // Held forward (+) or back (-) bend of the body over the whole clip.
  float lean = 0.0F;
  // Lunge: the part of the clip at which the blow lands (0 to 1).
  float strike = 0.0F;
  // Recoil: the blow comes from behind, so the body goes forward.
  bool fromBehind = false;

  friend bool operator==(const MotionRecipe&, const MotionRecipe&) = default;
};

enum class SpinAxis : std::uint8_t { X, Y, Z };

// Joints that turn on their own all the time (a gear's wheels).
struct SpinBones {
  std::vector<std::uint32_t> joints = {};
  SpinAxis axis = SpinAxis::X;
  float turnsPerSecond = 0.0F;

  friend bool operator==(const SpinBones&, const SpinBones&) = default;
};

// Which joints (the skin's, as the original's object index) the made motions move.
struct BoneGroups {
  std::uint32_t body = 1;
  std::vector<std::uint32_t> head = {};
  std::vector<std::uint32_t> leftArm = {};
  std::vector<std::uint32_t> rightArm = {};
  std::vector<std::uint32_t> leftLeg = {};
  std::vector<std::uint32_t> rightLeg = {};
  std::optional<SpinBones> spin = {};
  // The body never leans forward or back (a wheel's body): what a motion would lean it moves it
  // forward or back instead.
  bool upright = false;

  friend bool operator==(const BoneGroups&, const BoneGroups&) = default;
};

enum class PartnerClipMode : std::uint8_t {
  // The donor's clips moved onto this skeleton through the bone map.
  Retarget,
  // Clips made from the model's own idle with the motions; the donor gives each clip's length,
  // loops and root motion, so the battle's frames line up with the donor's.
  Procedural,
};

// How one species gets its partner clips.
struct PartnerClipRecipe {
  // The partner form whose clips are moved over (Retarget) or whose clips time the made ones
  // (Procedural). Its technique clips (from firstTechnique) are made too.
  std::string donor = {};
  PartnerClipMode mode = PartnerClipMode::Retarget;
  // Joint of this model -> joint of the donor. `sameBones` maps every joint to itself (the same
  // hierarchy). A Procedural recipe uses the map only for its technique effects' points.
  bool sameBones = false;
  std::map<std::uint32_t, std::uint32_t> bones = {};
  // Clips of its own the partner keeps; the others of the partner set are replaced.
  std::vector<std::uint32_t> keep = {};
  std::optional<BoneGroups> groups = {};
  // Motions of this species that replace the book's for these clip ids.
  std::map<std::uint32_t, MotionRecipe> motions = {};

  // The donor's joint of a joint of this model, if mapped.
  [[nodiscard]] std::optional<std::uint32_t> donorJoint(std::uint32_t joint) const;
};

struct PartnerClipBook {
  // The clip ids the partner's field and battle code play (care, gestures, battle stances,
  // reactions); the technique clips go from firstTechnique, one per move slot.
  std::vector<std::uint32_t> clips = {};
  std::uint32_t firstTechnique = 0;
  // The made motion of each clip id, and the one of every technique clip.
  std::map<std::uint32_t, MotionRecipe> motions = {};
  MotionRecipe technique = {};
  std::map<std::string, PartnerClipRecipe, std::less<>> species = {};

  // The recipe of the species with this id, or nullptr.
  [[nodiscard]] const PartnerClipRecipe* find(std::string_view id) const;
  // The motion a species makes for a clip id: its own, else the book's (technique for the
  // technique clips); nothing when neither has one.
  [[nodiscard]] std::optional<MotionRecipe> motionOf(const PartnerClipRecipe& recipe,
                                                     std::uint32_t clip) const;
};

// The asset id of the game pack's book.
inline constexpr std::string_view PartnerClipBookAsset = "animations/partner_clips";

// Parses the book. Errors name the line and column.
core::Result<PartnerClipBook> parsePartnerClipBook(std::string_view yaml);

// Reads and parses the book with this asset id.
core::Result<PartnerClipBook> loadPartnerClipBook(const VirtualFileSystem& vfs, const AssetId& id);

} // namespace dwe::assets
