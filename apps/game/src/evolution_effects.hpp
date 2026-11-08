#pragma once

#include <dwe/ruleset/random.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "body_face.hpp"
#include "scene_prims.hpp"

namespace dwe::app {

// effect_common.c: lerp, kept whole: past t1 the result wraps instead of stopping at `end`.
[[nodiscard]] std::int32_t originalLerp(std::int32_t start, std::int32_t end, std::int32_t t0,
                                        std::int32_t t1, std::int32_t t);

// A primitive with the depth the original sorts it by (its ordering-table key).
struct SortedPrim {
  float depth = 0.0F;
  ScenePrim prim;
};

// The primitives back to front, the first of equal depths first.
[[nodiscard]] std::vector<ScenePrim> sortedPrims(std::vector<SortedPrim> prims);

// model.c: WIREFRAME_RNG_TABLE[index & 15]: renderWireframed draws a face textured when it is below
// the share, and the death breaks the faces whose entry is a wave's (dooa.c:
// DOOA_spawnBoneShards).
[[nodiscard]] std::int32_t wireTableEntry(std::uint32_t index);

// The partner as the evolution's effects see it: where its bones are and the faces of each, posed
// in the world now.
class EvolutionBody {
public:
  EvolutionBody() = default;
  EvolutionBody(const EvolutionBody&) = delete;
  EvolutionBody& operator=(const EvolutionBody&) = delete;
  EvolutionBody(EvolutionBody&&) = delete;
  EvolutionBody& operator=(EvolutionBody&&) = delete;
  virtual ~EvolutionBody() = default;

  // The origin of a bone (calculateBoneMatrix's translation); nothing for a bone the model lacks.
  [[nodiscard]] virtual std::optional<glm::vec3> bonePoint(std::uint32_t bone) const = 0;
  // The faces of one bone; none for a bone without an object (DIGIMON_SKELETONS objIndex -1).
  [[nodiscard]] virtual std::vector<BodyFace> boneFaces(std::uint32_t bone) const = 0;
  // How many bones the model has (DIGIMON_DATA boneCount).
  [[nodiscard]] virtual std::uint32_t boneCount() const = 0;
};

// A flash of efe.c: EFECreateFlash, as the evolution makes them.
struct EvolutionFlash {
  glm::vec3 position{0.0F};
  // 0 or 1: which half of the glow image it draws (MAIN_D_8013421C); 0x20 draws half 0
  // subtracted from the screen (renderEFEFlash: getTPage(1, 2, ...)).
  std::int32_t mode = 0;
  // An ordering-table entry it is sorted at, or <= 0 to sort it at its depth. An entry up to
  // FlashFrontOrder draws in front of everything, a farther one behind everything.
  std::int32_t fixedDepth = -1;
  std::int32_t length = 0;
  std::int32_t scaleFrom = 0;
  std::int32_t scaleTo = 0;
  glm::ivec3 colorFrom{0};
  glm::ivec3 colorTo{0};
  std::int32_t progress = 0;
};

inline constexpr std::int32_t FlashFrontOrder = 0x22;
inline constexpr std::int32_t FlashSubtractMode = 0x20;

// efe.c: renderEFEFlash: a flash's glow (the glow image), mirrored into four around its point.
void addFlashPrims(const EvolutionFlash& flash, const ScenePrimFrame& frame, float focalLength,
                   rhi::TextureId image, std::vector<SortedPrim>& out);

// model.c: renderWireframed's grey for an object's lines: WIREFRAME_COLOR_MIN + rand() %
// (WIREFRAME_COLOR_MAX - WIREFRAME_COLOR_MIN).
inline constexpr std::int32_t WireGreyMin = 0x37;
inline constexpr std::int32_t WireGreyMax = 0xFF;

// model.c: renderWireframed for one object: of its faces turned to the camera, those whose
// WIREFRAME_RNG_TABLE entry is below `share` textured, the others as lines of `grey` (0 to 255).
// `shade` gives a textured face's colour (NormalColorCol3's lighting); white without it.
void addWiredFaces(std::span<const BodyFace> faces, std::int32_t share, std::int32_t grey,
                   const ScenePrimFrame& frame, rhi::TextureId white, std::vector<SortedPrim>& out,
                   const std::function<glm::vec4(const BodyFace&)>& shade = {});

// An expanding square on the ground (EVL_spawnParticle).
struct EvolutionRing {
  glm::ivec3 color{0};
  std::int32_t timer = 0;
};

// The faces of a broken bone flying apart (EVL_buildShardSet).
struct EvolutionShardSet {
  std::vector<BodyFace> faces;
  std::int32_t timer = 0;
};

// A ray from the body's middle through a broken bone (EVL_spawnSpark).
struct EvolutionSpark {
  std::uint32_t bone = 0;
  std::int32_t timer = 0;
};

// The images the effects draw with.
struct EvolutionImages {
  rhi::TextureId white{};
  // ETCDAT/SPOT256.TIM (128 x 128), alpha 255 wherever drawn.
  std::optional<rhi::TextureId> glow;
  // ETCDAT/DEFEFF.TIM with its palette 1 (256 x 48), alpha 255 wherever drawn.
  std::optional<rhi::TextureId> spark;
};

// What one drawn frame of the effects needs to know of the scene.
struct EvolutionView {
  ScenePrimFrame frame;
  // The scene's projection distance (VIEWPORT_DISTANCE).
  float focalLength = 1000.0F;
  EvolutionImages images;
};

// The visual effects of the evolution scene (evl.c: EVL_tickEvoSequence and its objects): the
// bones break one by one in a random order into shards that fly apart, each leaving its bone as a
// wireframe, a ray from the body's middle and a flash; green squares spread on the ground; the
// palette brightens; flashes grow over the body before the new form shows, and the new form fills
// in from a wireframe.
class EvolutionEffects {
public:
  // EVL_initEvoSequence: the order the bones 1 to boneCount - 1 break in, from the original's
  // rand() with this state; the squares centre on the partner's feet.
  void start(std::uint32_t boneCount, glm::vec3 feet, std::uint32_t seed);
  // Nothing left drawn or waiting (EVL_releaseAllParticles; state 3).
  void clear();

  // State 1 ends (frame 0x6a): the first bone breaks.
  void burst(std::int32_t frame, const EvolutionBody& body);
  // State 2 on this frame of the scene.
  void step(std::int32_t frame, const EvolutionBody& body);
  // The objects the scene made tick (EVL_tickParticle, EVL_tickShardSet, EVL_tickSpark,
  // tickEFEFlash).
  void tickObjects();

  // The scene's sounds of slot 8 since the last call (a breaking bone's).
  [[nodiscard]] std::vector<std::int32_t> takeSounds();

  // PARTNER_WIREFRAME_SUB[bone] is 0: the bone is drawn as wireframe.
  [[nodiscard]] bool boneWired(std::uint32_t bone) const;
  // PARTNER_WIREFRAME_TOTAL: how many of 16 of the whole body's faces are drawn textured, 16 when
  // the body is drawn as usual.
  [[nodiscard]] std::int32_t bodyShare() const { return bodyShare_; }
  // EVL_brightenDigimonClut on this frame: how far toward white each channel moved.
  [[nodiscard]] glm::vec3 lift() const { return lift_; }

  [[nodiscard]] const std::vector<EvolutionRing>& rings() const { return rings_; }
  [[nodiscard]] const std::vector<EvolutionShardSet>& shards() const { return shards_; }
  [[nodiscard]] const std::vector<EvolutionSpark>& sparks() const { return sparks_; }
  [[nodiscard]] const std::vector<EvolutionFlash>& flashes() const { return flashes_; }
  // The bones in the order they break.
  [[nodiscard]] const std::vector<std::uint32_t>& order() const { return order_; }

  // The frame's primitives, back to front: the squares, the shards, the rays, the flashes and the
  // wireframe (and the textured faces) of the body.
  [[nodiscard]] std::vector<ScenePrim> draw(const EvolutionBody& body, const EvolutionView& view);

private:
  void breakNext(std::int32_t frame, const EvolutionBody& body);
  void addFlash(const EvolutionFlash& flash);
  std::int32_t random();

  ruleset::Random random_{0};
  std::uint32_t boneCount_ = 0;
  glm::vec3 feet_{0.0F};
  std::vector<std::uint32_t> order_;
  std::size_t broken_ = 0;
  std::vector<bool> wired_;
  std::int32_t bodyShare_ = 16;
  glm::vec3 lift_{0.0F};
  std::vector<EvolutionRing> rings_;
  std::vector<EvolutionShardSet> shards_;
  std::vector<EvolutionSpark> sparks_;
  std::vector<EvolutionFlash> flashes_;
  std::vector<std::int32_t> sounds_;
};

} // namespace dwe::app
