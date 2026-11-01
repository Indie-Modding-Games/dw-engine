#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/image.hpp>
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

namespace dwe::assets {

class VirtualFileSystem;

// One vertex of a model, ready to upload as it is. Attributes a primitive does not have take
// neutral values: normal (0, 1, 0), uv (0, 0), colour (1, 1, 1, 1), joint 0 with weight 1.
struct ModelVertex {
  glm::vec3 position{0.0F};
  glm::vec3 normal{0.0F, 1.0F, 0.0F};
  glm::vec2 uv{0.0F};
  // Multiplies the texel, or is the colour without a texture. The extracted models store 128 as 1.0
  // with a texture, as the original's GPU did, so a value may go up to about 2.
  glm::vec4 color{1.0F};
  std::array<std::uint16_t, 4> joints{};
  glm::vec4 weights{1.0F, 0.0F, 0.0F, 0.0F};
};

// A set of triangles drawn with one material.
struct ModelPrimitive {
  std::vector<ModelVertex> vertices;
  std::vector<std::uint32_t> indices; // three per triangle, counter-clockwise front faces
  std::uint32_t material = 0;
  bool hasNormals = false;
  bool hasUvs = false;
  bool hasColors = false;
};

enum class AlphaMode : std::uint8_t {
  Opaque,
  // Texels below the material's cutoff are not drawn (the original's transparent texels).
  Mask,
  // Semi-transparent: drawn after the opaque ones with the material's blending.
  Blend,
};

// The original's four semi-transparency equations, B the framebuffer and F the new colour
// (extras.blending).
enum class SemiTransparency : std::uint8_t {
  Average,    // B / 2 + F / 2
  Add,        // B + F
  Subtract,   // B - F
  AddQuarter, // B + F / 4
};

// A place in the console's VRAM: x in 16-bit units, y in rows.
struct VramPoint {
  std::uint32_t x = 0;
  std::uint32_t y = 0;

  bool operator==(const VramPoint&) const = default;
};

struct ModelMaterial {
  std::string name;
  glm::vec4 baseColor{1.0F};
  // Index into ModelData::images.
  std::optional<std::uint32_t> image;
  AlphaMode alphaMode = AlphaMode::Opaque;
  float alphaCutoff = 0.5F;
  // KHR_materials_unlit: the colour is not lit by the map's lights.
  bool unlit = false;
  bool doubleSided = false;
  SemiTransparency blending = SemiTransparency::Average;
  // Where the original's texture of the material and its CLUT row sit in VRAM, in 16-bit units
  // (extras.image, extras.clut), when the model says.
  std::optional<VramPoint> texture;
  std::optional<VramPoint> clut;
};

// A node's local transform as translation, rotation and scale.
struct NodeTransform {
  glm::vec3 translation{0.0F};
  glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
  glm::vec3 scale{1.0F};

  [[nodiscard]] glm::mat4 matrix() const;
};

struct ModelNode {
  std::string name;
  std::optional<std::uint32_t> parent;
  NodeTransform rest;
};

// The joints of the mesh: node indices and the matrices that take a vertex from model space to each
// joint's space.
struct ModelSkin {
  std::vector<std::uint32_t> joints;
  std::vector<glm::mat4> inverseBindMatrices;
};

enum class ChannelPath : std::uint8_t { Translation, Rotation, Scale };
enum class Interpolation : std::uint8_t { Linear, Step };

// The keyframes that move one property of one node. Rotations are quaternions stored as (x, y, z,
// w); translations and scales use x, y, z.
struct AnimationChannel {
  std::uint32_t node = 0;
  ChannelPath path = ChannelPath::Translation;
  Interpolation interpolation = Interpolation::Linear;
  std::vector<float> times; // seconds, increasing
  std::vector<glm::vec4> values;
};

// A loop the original ran (extras.loops): after lastTick it continues as from firstTick, `count`
// times in all, or forever when `count` is empty. A loop from tick 0 has no tick before its start,
// so the step back to it does not move the root.
struct AnimationLoop {
  std::uint32_t firstTick = 0;
  std::uint32_t lastTick = 0;
  std::optional<std::uint32_t> count;
};

// Where a clip repeats forever, in seconds: after `end` it goes on from `start`, the step from
// `end` back to `start` taking `gap` seconds (one tick for the extracted clips, whose samples stop
// at the first jump back).
struct ClipRepeat {
  float start = 0.0F;
  float end = 0.0F;
  float gap = 0.0F;
};

// A sound the clip plays (extras.sounds; anim.c: tickAnimation, instruction 0x4000) on the tick its
// stream reaches it: `bank` is the original's sound slot, 4 being the entity's own species bank.
struct ClipSound {
  std::uint32_t tick = 0;
  std::uint8_t bank = 0;
  std::uint8_t sound = 0;

  bool operator==(const ClipSound&) const = default;
};

struct AnimationClip {
  std::string name;
  // The original's animation id (extras.id), the one scripts and the models' YAML use.
  std::optional<std::uint32_t> id;
  float duration = 0.0F; // seconds, the last keyframe of any channel
  // The original's sampling (extras): 0 when the clip does not say.
  std::uint32_t ticksPerSecond = 0;
  std::uint32_t tickCount = 0;
  std::uint32_t frameCount = 0;
  std::vector<AnimationLoop> loops; // counted loops are already unrolled in the keyframes
  bool endsInLoop = false;
  // Set when the clip repeats forever (endsInLoop): its last loop.
  std::optional<ClipRepeat> repeat;
  // The node whose translation is the entity's movement (extras.rootMotionNode), not part of the
  // pose.
  std::optional<std::uint32_t> rootMotionNode;
  std::vector<AnimationChannel> channels;
  // In tick order; a clip that repeats plays the ones of its repeat on every round.
  std::vector<ClipSound> sounds;
};

// A model loaded for the runtime: what render draws and animation samples. Every index in it has
// been checked.
struct ModelData {
  std::vector<ModelNode> nodes;
  // Every node, each after its parent.
  std::vector<std::uint32_t> nodeOrder;
  std::vector<ModelPrimitive> primitives;
  std::vector<ModelMaterial> materials;
  std::vector<Image> images; // RGBA8
  // A model without a skin draws its primitives in model space (as if every vertex used one
  // identity joint).
  std::optional<ModelSkin> skin;
  std::vector<AnimationClip> clips;

  // The clip with this original animation id, or nullptr.
  [[nodiscard]] const AnimationClip* findClip(std::uint32_t id) const;
  // The clip with this name, or nullptr.
  [[nodiscard]] const AnimationClip* findClip(std::string_view name) const;
};

// Fills the clips a model lacks with copies of others (a stand-in id -> the id it copies): an
// enemy model made a partner (the hidden content) has no care or gym clips. A clip the model has,
// or one whose stand-in it lacks too, is left alone. The ids added, in order.
std::vector<std::uint32_t> addClipStandIns(ModelData& model,
                                           const std::map<std::uint32_t, std::uint32_t>& standIns);

// The most a model may have. The extracted ones stay far below; the limits keep a hostile file
// bounded.
inline constexpr std::size_t MaxModelVertices = std::size_t{1} << 20U;
inline constexpr std::size_t MaxModelNodes = 1024;
inline constexpr std::size_t MaxModelKeyframes = std::size_t{1} << 22U;
inline constexpr std::size_t MaxModelIndices = std::size_t{1} << 22U;
// Decoded pixels of all the model's images together (64 MiB of RGBA8), checked from the PNG headers
// before decoding.
inline constexpr std::size_t MaxModelImagePixels = std::size_t{1} << 24U;

// Parses a binary glTF (.glb) with its buffer and images embedded, as dwx extracts the models, and
// checks it. Only what the runtime draws is supported: triangle lists, one skin, TRS nodes, linear
// and step keyframes, PNG images.
core::Result<ModelData> parseModel(std::span<const std::byte> glb);

// Reads and parses the model with this asset id (for example `digimon/species_a/model`).
core::Result<ModelData> loadModel(const VirtualFileSystem& vfs, const AssetId& id);

} // namespace dwe::assets
