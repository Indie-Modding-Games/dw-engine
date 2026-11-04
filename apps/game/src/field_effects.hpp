#pragma once

#include <dwe/rhi/bindless.hpp>
#include <dwe/ruleset/random.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "scene_prims.hpp"

namespace dwe::app {

// A point in the original's world axes and units (y down, z the other way from the world's).
using OriginalPoint = glm::ivec3;

// The images the field's effects draw with, each keeping the original's STP bits
// (FieldScene::stpSceneImage).
struct FieldEffectImages {
  // ETCDAT/ETCTIM.BIN entry 2 (VRAM 768, 384; 256 x 48 texels): the clouds with palette 0 (CLUT
  // 192, 488), the particles with palette 1 (CLUT 192, 489).
  std::optional<rhi::TextureId> clouds;
  std::optional<rhi::TextureId> particles;
  // Entry 3 (VRAM 960, 384; 256 x 64) with palette 2 (CLUT 272, 482).
  std::optional<rhi::TextureId> butterfly;
  // ETCDAT/SPOT256.TIM (128 x 128), the sparks' flash.
  std::optional<rhi::TextureId> glow;
  // Entry 2 with palette 2 (CLUT 192, 490): the particles a spark leaves on a fighter.
  std::optional<rhi::TextureId> fighterParticles;
};

// A cloud of smoke or dust (efe.c: CLOUD_FX_DATA): it rises and fades over 14 frames.
struct FieldCloud {
  std::int32_t x = 0;
  std::int32_t z = 0;
  std::int32_t state = 0;
};

// A spark of light that bursts into particles (efe.c: PARTICLE_FX_DATA).
struct FieldSpark {
  OriginalPoint position{0};
  // createParticleFX's `count`: 0, 1 (a hit) or 2 (a block).
  std::int32_t mode = 0;
  glm::ivec3 color{0};
  std::int32_t state = 0;
  std::int32_t timer = 0;
  std::int32_t endFrame = 0;
  // spread[0..3][i]: each flash's offset and size.
  std::array<std::array<std::int32_t, 3>, 4> spread{};
};

// A particle on a fighter's node (efe.c: renderEntityParticleFX): where the node is now (world
// axes), which of ENTITY_PARTICLE_FX_MODES it looks like and its timer, which steps its picture.
struct FieldFighterMote {
  glm::vec3 position{0.0F};
  std::int32_t look = 0;
  std::int32_t timer = 0;
};

// What a battle adds to the frame (game::battle::HitParticles): its sparks and clouds, drawn as the
// field's, and the particles on the fighters.
struct FieldBattleParticles {
  std::vector<FieldSpark> sparks;
  std::vector<FieldCloud> clouds;
  std::vector<FieldFighterMote> motes;
};

// What one drawn frame of the effects needs to know of the scene.
struct FieldEffectView {
  ScenePrimFrame frame;
  // The map's projection distance (VIEWPORT_DISTANCE).
  float focalLength = 1000.0F;
  FieldEffectImages images;
  // Where each butterfly circles (effect_common.c: translateConditionFXToEntity of the partner),
  // in the world's axes; nothing draws none.
  std::optional<glm::vec3> butterflyCentre;
  // A battle's sparks, clouds and fighter particles; none outside one.
  const FieldBattleParticles* battle = nullptr;
};

// One particle of the healing effect (particle.c: ParticleObjEntry).
struct HealingMote {
  std::int32_t counter = -1;
  OriginalPoint position{0};
  // pos.pad: the spiral's particles' size, rolled every frame.
  std::int32_t size = 0;
};

// The particles over the partner as an item heals it (particle.c: HEALING_PARTICLES).
struct FieldHealing {
  std::int32_t frame = 0;
  // Where the spiral starts: the partner's x and z, the y over its head.
  OriginalPoint origin{0};
  std::int32_t radius = 0;
  // hasParticle2: the larger items add a ring of particles.
  bool ring = false;
  std::array<HealingMote, 20> spiral{};
  std::array<HealingMote, 20> rings{};
};

// A particle a scene places itself each frame (effect_common.c: renderFXParticle): the ending's
// sparks (endi.c: ENDI_renderParticles).
struct FieldGlow {
  OriginalPoint position{0};
  std::int32_t size = 0;
  glm::ivec3 color{0};
};

// The butterfly that flies around the partner while it sulks (butterfly.c).
struct FieldButterfly {
  std::int32_t frame = 0;
};

// The field's own effects, as the original's object list runs them a frame at a time: the clouds
// (efe.c: createCloudFX, tickCloudFX, renderCloudFX), the sparks (createParticleFX, tickParticleFX,
// renderParticleFX), the healing particles (particle.c) and the butterfly (butterfly.c).
class FieldEffects {
public:
  // The particles spread along the 50 directions initializeParticleFX rolled at boot (measured).
  explicit FieldEffects(std::uint64_t seed);

  // How a cloud or a spark comes: from a routine run by the original's object list (the gym's), so
  // tickObjects also runs it on the frame it is made and it is first drawn one frame on (state 1),
  // or from outside it (a pile of poop, createPoopPile), first drawn as made (state 0). Measured in
  // the original: TRN_tickDefenseTraining's clouds and sparks, and createPoopPile's cloud.
  enum class Birth : std::uint8_t { OutsideObjects, InObjects };

  // createCloudFX: at most 60 at once; the cloud's height is its own.
  void addCloud(std::int32_t x, std::int32_t z, Birth birth = Birth::OutsideObjects);
  // removeAllCloudFX.
  void removeClouds() { clouds_.clear(); }
  // createParticleFX's colour of a kind (fx->r, g, b); none for an unknown kind.
  [[nodiscard]] static std::optional<glm::ivec3> sparkColor(std::int32_t kind);
  // createParticleFX(kind, count, position, NULL, lifetime): at most 4 at once; an unknown kind or
  // count makes none.
  void addSpark(std::int32_t kind, std::int32_t mode, OriginalPoint position, std::int32_t lifetime,
                Birth birth = Birth::OutsideObjects);
  // addHealingParticleEffect: one at a time; `feet` is where the partner stands, `height` and
  // `radius` its species' (DIGIMON_DATA). False when one is already running.
  bool addHealing(OriginalPoint feet, std::int32_t height, std::int32_t radius, bool ring);
  // setButterfly and unsetButterfly: one at a time here (the partner's). None at first or after
  // clear() (butterfly.c: initializeButterfly).
  void setButterfly();
  void unsetButterfly() { butterfly_.reset(); }
  [[nodiscard]] bool hasButterfly() const { return butterfly_.has_value(); }
  // The particles a scene draws this frame, in place of the last ones.
  void setGlows(std::vector<FieldGlow> glows) { glows_ = std::move(glows); }
  // Everything gone (a map change: the original's objects of the field go with it).
  void clear();

  // One frame of the original's object list for these objects (tickCloudFX, tickParticleFX,
  // tickHealingParticles, tickButterfly). The butterfly waits while `inMenu` (IS_IN_MENU).
  void tickFrame(bool inMenu = false);
  // Real seconds of the field; whole frames of `frameSeconds` tick.
  void advance(double seconds, double frameSeconds, bool inMenu = false);

  // The frame's primitives, back to front: the clouds, the sparks' flashes and particles, the
  // fighters' particles, the healing particles and the butterfly.
  [[nodiscard]] std::vector<ScenePrim> draw(const FieldEffectView& view) const;

  [[nodiscard]] const std::vector<FieldCloud>& clouds() const { return clouds_; }
  [[nodiscard]] const std::vector<FieldSpark>& sparks() const { return sparks_; }
  [[nodiscard]] const std::optional<FieldHealing>& healing() const { return healing_; }
  [[nodiscard]] const std::optional<FieldButterfly>& butterfly() const { return butterfly_; }
  [[nodiscard]] bool empty() const {
    return clouds_.empty() && sparks_.empty() && !healing_ && !butterfly_ && glows_.empty();
  }

private:
  // libc rand: 15 bits.
  std::int32_t rand();
  void tickHealing(FieldHealing& healing);

  ruleset::Random random_;
  std::vector<FieldGlow> glows_;
  std::vector<FieldCloud> clouds_;
  std::vector<FieldSpark> sparks_;
  // One effect at a time (NUM_HEALING_INSTANCES 1), none at the start (particle.c:
  // initializeHealingParticles sets its frameId to -1).
  std::optional<FieldHealing> healing_;
  std::optional<FieldButterfly> butterfly_;
  double pending_ = 0.0;
};

// The smoke a character vanishes in (overworld.c: createNinjamonEffect, renderNinjamonEffect): the
// map's first 41 sprite objects drift right and down by their own steps, start over from where they
// were once past x 160, and all hide after 60 frames.
class ObjectSmoke {
public:
  static constexpr std::size_t Objects = 0x29;

  explicit ObjectSmoke(std::uint64_t seed) : random_(seed) {}

  // createNinjamonEffect: keeps where the objects are (storeMapObjectPosition) and rolls each one's
  // steps.
  void start(std::span<const glm::ivec2> places);
  [[nodiscard]] bool active() const { return active_; }
  void stop() { active_ = false; }
  // One frame of renderNinjamonEffect over the objects' places; true on the frame they all hide
  // (the smoke then ends).
  bool frame(std::span<glm::ivec2> places);

private:
  // randomLimit(10) + from.
  std::int32_t roll(std::int32_t from) { return random_.below(StepRoll) + from; }
  static constexpr std::int32_t StepRoll = 10;

  ruleset::Random random_;
  std::array<glm::ivec2, Objects> stored_{};
  std::array<glm::ivec2, Objects> steps_{};
  std::int32_t counter_ = 0;
  bool active_ = false;
};

} // namespace dwe::app
