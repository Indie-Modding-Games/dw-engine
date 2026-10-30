#pragma once

#include <dwe/ruleset/random.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "body_face.hpp"
#include "evolution_effects.hpp"
#include "scene_prims.hpp"

namespace dwe::app {

// The iris of a lost life and of a death (murd.c: MURD_renderIris, dooa.c: DOOA_renderIrisWindow):
// its size on this frame of a close from `start` to `end`, 200 down to 0 in 4.12 of the glow
// image's scale; frames past the end count as the end.
[[nodiscard]] std::int32_t irisRadius(std::int32_t start, std::int32_t end, std::int32_t frame);

// MURD_isDigimonLargerThanIris: whether the iris is no larger than the partner's box (getDistance
// of twice its radius, its height and twice its radius) seen at `partnerPixels` original pixels;
// from then on the partner is drawn over it.
[[nodiscard]] bool irisWithinPartner(std::int32_t radius, float partnerPixels);

// The iris around a clip point: the glow image's ring (SPOT256's top left quarter, mirrored into
// four) and four rectangles of its white texel to the screen's edges, subtracted from the picture,
// so that only a hole of the radius stays.
[[nodiscard]] std::vector<ScenePrim> irisPrims(glm::vec4 centre, std::int32_t radius,
                                               rhi::TextureId spot);

// A face broken off the dying partner (dooa.c: DooaShard and DooaShardQuad), in the original's axes
// (Y down) and units: its corners around its centre, the centre from the partner's feet.
struct DeathShard {
  std::uint8_t count = 3;
  std::array<glm::ivec3, 4> vertex{};
  std::array<glm::vec2, 4> uvs{};
  rhi::TextureId texture{};
  glm::ivec3 centre{0};
  std::int32_t fallSpeed = 0;
  // 4096 for 1: how far out from the axis the centre is drawn.
  std::int32_t radius = 0;
  std::int32_t rotY = 0;
  std::int32_t spin = 0;
  std::int32_t spinMax = 0;
  std::int32_t targetRadius = 0;
  std::int32_t dropDepth = 0;
  // Frames before it shrinks; -1 once it is gone.
  std::int32_t delay = 0;
  std::int32_t axisDistance = 1;
};

// A face of a broken egg piece (doo2.c: Doo2Shard) in the original's axes: it flies along its
// normal and falls until it lies on the ground.
struct EggShard {
  BodyFace face;
  // The mean of its first three normals, 4096 long.
  glm::ivec3 centre{0};
  glm::ivec3 offset{0};
  std::int32_t frame = 0;
};

// The faces of one broken egg piece (doo2.c: DOO2_buildShardSet).
struct EggShardSet {
  std::vector<EggShard> shards;
  std::int32_t timer = 0;
};

// What one drawn frame of the effects needs to know of the scene.
struct DeathView {
  ScenePrimFrame frame;
  // The scene's projection distance (VIEWPORT_DISTANCE).
  float focalLength = 1000.0F;
  rhi::TextureId white{};
  // ETCDAT/SPOT256.TIM, every drawn texel opaque.
  std::optional<rhi::TextureId> glow;
  // ETCDAT/DEFEFF.TIM with its palette 1.
  std::optional<rhi::TextureId> spark;
};

// The partner as the death's effects see it: where it stands and how tall it is.
struct DeathPartner {
  glm::vec3 feet{0.0F};
  float height = 0.0F;
};

// The visual effects of the death (dooa.c: DOOA_tickDissolve and the shard effect,
// DOOA_updateShards) and of the rebirth (DOOA_tickRebirth, DOOA_renderRebirth, doo2.c): the body
// turns to wireframe in waves while its faces break off and fall, a light comes down where it
// stood, the shards flicker, rise in a spiral and close into the egg, which fills in, flashes,
// throws sparks and breaks in pieces around the new baby.
class DeathEffects {
public:
  // Nothing drawn or waiting; the effects' own rand() starts from this state.
  void start(std::uint32_t seed);
  void clear();

  // DOOA_tickDissolve on this frame (counted from the death's start), after the scene's own step:
  // the wireframe waves and their shards (0x69 to 0x8C), the light coming down (0xD2 to 0x135)
  // and the flicker of the shards (0x135).
  void dissolveStep(std::int32_t frame, const EvolutionBody& body, const DeathPartner& partner);
  // DOOA_tickRebirth on this frame of the rebirth; `eggSlot` picks the egg of TAMA.TMD (the
  // baby's place among REINCARNATE_BABY_TYPE).
  void rebirthStep(std::int32_t frame, std::int32_t eggSlot,
                   const std::vector<std::vector<BodyFace>>* egg);
  // The objects the scene made tick: the shards (DOOA_updateShards), the flashes (tickEFEFlash)
  // and the egg's broken pieces (DOO2_tickShardSet).
  void tickObjects();

  // Egg pieces broken since the last call: each plays the sparkle sound.
  [[nodiscard]] std::int32_t takePieceBreaks() { return std::exchange(pieceBreaks_, 0); }

  // PARTNER_WIREFRAME_TOTAL: how many of 16 of the body's faces are drawn textured; 16 is the body
  // as usual, drawn by the field.
  [[nodiscard]] std::int32_t bodyShare() const { return bodyShare_; }
  [[nodiscard]] const std::vector<DeathShard>& shards() const { return shards_; }
  [[nodiscard]] bool flicker() const { return flicker_; }
  [[nodiscard]] std::int32_t shardState() const { return shardState_; }
  [[nodiscard]] const std::vector<EvolutionFlash>& flashes() const { return flashes_; }
  [[nodiscard]] std::int32_t eggShare() const { return eggShare_; }
  [[nodiscard]] bool eggShown() const { return eggSlot_.has_value(); }
  [[nodiscard]] const std::vector<EggShardSet>& eggShards() const { return eggShards_; }
  [[nodiscard]] std::int32_t streaks() const { return streaks_; }

  // The frame's primitives, back to front: the wireframe body, the shards, the flashes, the egg,
  // its broken pieces and the sparks.
  [[nodiscard]] std::vector<ScenePrim> draw(const EvolutionBody& body, const DeathView& view);

private:
  void spawnWave(const EvolutionBody& body, std::int32_t wave);
  void tickShards();
  void addFlash(const EvolutionFlash& flash, float offsetX);
  void breakEggPiece(std::int32_t piece);
  std::int32_t random();

  ruleset::Random random_{0};
  std::int32_t bodyShare_ = 16;
  // DOOA_SHARD_EFFECT: where the shards hang (the partner's feet, its height stored) and its state.
  glm::ivec3 anchor_{0};
  float partnerHeight_ = 0.0F;
  std::vector<DeathShard> shards_;
  std::int32_t shardState_ = 0;
  std::int32_t shardPrevState_ = 0;
  bool flicker_ = false;
  glm::ivec3 shardColor_{0x50};
  std::vector<EvolutionFlash> flashes_;
  // A flash's sideways move (setEFEFlashOffset), in world units at its point, by flash.
  std::vector<float> flashOffsets_;
  // The rebirth's egg: its slot, how much of it is textured and which pieces are broken.
  std::optional<std::int32_t> eggSlot_;
  const std::vector<std::vector<BodyFace>>* egg_ = nullptr;
  std::int32_t eggShare_ = 0;
  std::array<bool, 6> pieceGone_{};
  std::vector<EggShardSet> eggShards_;
  std::int32_t pieceBreaks_ = 0;
  std::int32_t streaks_ = 0;
};

} // namespace dwe::app
