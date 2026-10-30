#pragma once

#include <dwe/assets/effect_script.hpp>
#include <dwe/game/battle/state.hpp>
#include <dwe/ruleset/random.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dwe::game::battle {

// A vector of the effect scripts (battle_effect.c: EfeVector, EfeColor), in the console's axes
// (y down) and units.
struct EffectVector {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;

  bool operator==(const EffectVector&) const = default;
};

// A transform of the console (MATRIX): a rotation and scale where 4096 is 1, and a translation.
struct EffectMatrix {
  std::array<std::array<std::int32_t, 3>, 3> m{{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}};
  EffectVector t;

  bool operator==(const EffectMatrix&) const = default;
};

// What an effect knows of a fighter (its Entity and species data), in the console's axes.
struct EffectBody {
  // The species table's id of its original type (effect_common.c: getOriginalType), which the
  // bone-offset lists name.
  std::string species;
  // posData->location and rotation.vy (4096 per turn).
  EffectVector location;
  std::int32_t rotationY = 0;
  // The species data's radius and height.
  std::int32_t radius = 0;
  std::int32_t height = 0;
  // Whether it can still be picked (currentHP > 0, on the map).
  bool standing = true;
};

// A node of a fighter's model as the original poses it (PositionData), relative to its parent, in
// the console's axes: RotMatrix angles (4096 per turn), location, and scale (4096 for 1).
struct BonePose {
  EffectVector rotation;
  EffectVector location;
  EffectVector scale{.x = 4096, .y = 4096, .z = 4096};

  bool operator==(const BonePose&) const = default;
};

// effect_common.c: calculateBoneMatrix: the matrix of the last node of `chain` (the nodes from the
// model's root down to it, each relative to the one before) for a fighter standing at `place`. The
// root takes the place's facing and location, then each node is multiplied in (vecmath.c:
// calculatePosition, GsMulCoord3) in the console's 4.12 fixed point, which drops the fraction of
// every product: a node a few joints down lands up to three units from where exact maths puts it.
[[nodiscard]] EffectMatrix boneMatrix(const EffectBody& place, std::span<const BonePose> chain);

// Where the effects read the fighters: the battle's places and the models' nodes, which the app
// animates.
class EffectBodies {
public:
  EffectBodies() = default;
  EffectBodies(const EffectBodies&) = delete;
  EffectBodies& operator=(const EffectBodies&) = delete;
  EffectBodies(EffectBodies&&) = delete;
  EffectBodies& operator=(EffectBodies&&) = delete;
  virtual ~EffectBodies() = default;

  [[nodiscard]] virtual std::optional<EffectBody> body(FighterId fighter) const = 0;
  // A node of the fighter's model in the world (effect_common.c: calculateBoneMatrix; the node's
  // posMatrix.workm), the model's node 0 for one it lacks; nothing when the fighter is not drawn.
  [[nodiscard]] virtual std::optional<EffectMatrix> node(FighterId fighter,
                                                         std::int32_t index) const = 0;
  // The battle's fighters in the original's entity order (ENTITY_TABLE 1 to 9).
  [[nodiscard]] virtual std::vector<FighterId> fighters() const = 0;
  // Where a point of the original's world stands on the battle's ground (the way back from
  // body()'s location).
  [[nodiscard]] virtual Point ground(const EffectVector& at) const = 0;
};

// A fighter a hit check touched and the point of the hit, the attack object's position
// (graphics.c: addAttackObject), in the console's axes: the instance's for a box check or
// hit_target, the fighter's body toward the source for a radius or line check (onBody).
struct EffectTouch {
  FighterId fighter = 0;
  EffectVector at;
  bool onBody = false;

  bool operator==(const EffectTouch&) const = default;
};

// How a sprite mixes with what is behind it (GsSPRITE attribute bits 28 to 30): one of the four
// semi-transparency rates, or opaque.
enum class EffectBlend : std::uint8_t { Average, Add, Subtract, AddQuarter, Opaque };

// A sprite at a world point, facing the screen (battle_effect.c: BTL_renderProjectedSprite and
// BTL_renderCenteredSprite: GsSortSprite at the point's projection). Its size on screen is the
// texels' times scale / 4096 at the original's projection, which in the world is
// texels * scale / 32 units (scale * VIEWPORT_DISTANCE << 5 over the depth / 4).
struct EffectSprite {
  EffectVector position;
  // The texel colours times color / 128.
  EffectVector color{128, 128, 128};
  std::int32_t scaleX = 4096;
  std::int32_t scaleY = 4096;
  // The texels of the slot's texture, from its top left corner.
  std::int32_t u = 0;
  std::int32_t v = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  // The texel drawn on the point, which the sprite turns and scales around.
  std::int32_t pivotX = 0;
  std::int32_t pivotY = 0;
  // A clockwise turn on screen around the pivot, in degrees (GsSPRITE rotate / 4096).
  std::int32_t rotate = 0;
  EffectBlend blend = EffectBlend::Opaque;
  // The palette row of the slot's texture, from its first.
  std::int32_t clutRow = 0;

  bool operator==(const EffectSprite&) const = default;
};

// An object of the slot's model drawn with a world transform (battle_effect.c: BTL_drawTMD,
// BTL_drawTMDYXZ: the instance's position, its rotation and a scale), with the colour the scripts
// gave its primitives (BTL_setEFEModelObjectColor; 128 is 1) and the texels its UV animation moved
// them by (BTL_offsetEFEPrimitiveUVs).
struct EffectModel {
  std::int32_t object = 0;
  EffectMatrix transform;
  std::optional<EffectVector> color;
  std::int32_t uOffset = 0;
  std::int32_t vOffset = 0;

  bool operator==(const EffectModel&) const = default;
};

// A particle of a technique's emitter (battle_effect.c: BTL_renderParticleEmitters): a line from
// `from` to `to` in the emitter's colour (drawLine2P, opaque, 255 is full), or, for a glowing
// kind (0 and 2) whose `from` is nearer the camera than StreakGlowDepth, the common glow there
// (effect_common.c: renderFXParticle: StreakGlowSize units wide, added, tinted by the colour,
// 128 is 1).
struct EffectStreak {
  EffectVector from;
  EffectVector to;
  EffectVector color;
  bool glows = false;

  bool operator==(const EffectStreak&) const = default;
};

// The view depth from which a glowing streak is a line instead (depth >> 2 < 1000), and the glow's
// world size (renderFXParticle's 0x19).
inline constexpr std::int32_t StreakGlowDepth = 4000;
inline constexpr std::int32_t StreakGlowSize = 25;

// A puff of a technique's cloud (efe.c: createCloudFX, tickCloudFX, renderCloudFX): a 32 x 32
// frame of the common effect sheet (DEFEFF, its first row of frames and palette) at `u`, added,
// grey `shade` (128 is 1) and `scale` times 32 units wide (4096 is 1), facing the screen.
struct EffectCloud {
  EffectVector position;
  std::int32_t u = 0;
  std::int32_t shade = 128;
  std::int32_t scale = 4096;

  bool operator==(const EffectCloud&) const = default;
};

// A flash of a technique (efe.c: EFECreateFlash, tickEFEFlash, renderEFEFlash): the glow of the
// light sheet (SPOT256's 64 x 64 quarter at `u`, v 64, drawn four times mirrored around its
// corner) centred on `position`, tinted by `color` (128 is 1) and mixed by `blend`. Its half side
// in the original's pixels is (scale * 63 >> 10) + 1, with scale = focal * size * 10 / depth + bump
// (nothing drawn from 0x8000 on), and it sorts at `fixedDepth` * 16 of view depth when that is
// positive, at its own depth otherwise.
struct EffectFlash {
  EffectVector position;
  EffectVector color;
  std::int32_t size = 0;
  std::int32_t bump = 0;
  std::int32_t u = 0;
  EffectBlend blend = EffectBlend::Add;
  std::int32_t fixedDepth = 0;

  bool operator==(const EffectFlash&) const = default;
};

// A point on the screen, in the original's pixels from the screen's centre (x right, y down).
struct EffectScreenPoint {
  std::int32_t x = 0;
  std::int32_t y = 0;

  bool operator==(const EffectScreenPoint&) const = default;
};

// The original's 320 x 240 screen (main.c: GsInitGraph), around its centre.
inline constexpr EffectScreenPoint OriginalScreen{.x = 320, .y = 240};

// A quad a technique draws on the screen instead of in the world (battle_effect.c:
// BTL_renderScrollingBackground, BTL_renderParallaxSprites: a POLY_FT4 at screen coordinates).
// Corners 0, 1, 2 and 3 are the top left, top right, bottom left and bottom right; their texels
// run from (u, v) over texelWidth x texelHeight of the slot's texture with palette row clutRow,
// times color / 128, mixed by blend (a texel without STP is drawn opaque, as the console does). It
// sorts at entry `order` of the original's ordering table: 0 in front, 0xFFF at the back, 16 units
// of view depth each.
struct EffectScreenQuad {
  std::array<EffectScreenPoint, 4> corners{};
  std::int32_t u = 0;
  std::int32_t v = 0;
  std::int32_t texelWidth = 0;
  std::int32_t texelHeight = 0;
  EffectVector color{128, 128, 128};
  std::int32_t clutRow = 0;
  EffectBlend blend = EffectBlend::Opaque;
  std::int32_t order = 0;

  bool operator==(const EffectScreenQuad&) const = default;
};

using EffectShape = std::variant<EffectSprite, EffectModel, EffectStreak, EffectCloud, EffectFlash,
                                 EffectScreenQuad>;

// One thing a slot's render scripts drew this frame.
struct EffectDraw {
  std::size_t slot = 0;
  EffectShape shape;

  bool operator==(const EffectDraw&) const = default;
};

// Where the effect scripts' sounds go: bank 8 of the sound player (battle_effect.c:
// BTL_playEFESound). A held sound keeps its voice so the runner can stop it when its sub-effect
// ends.
class EffectSounds {
public:
  EffectSounds() = default;
  EffectSounds(const EffectSounds&) = delete;
  EffectSounds& operator=(const EffectSounds&) = delete;
  EffectSounds(EffectSounds&&) = delete;
  EffectSounds& operator=(EffectSounds&&) = delete;
  virtual ~EffectSounds() = default;

  // The slot is the effect's (EffectRunner::load) that asks for the sound.
  virtual void play(std::size_t slot, std::int32_t sound) = 0;
  // The voice the sound plays on, or nothing when it did not play.
  virtual std::optional<std::int64_t> playHeld(std::size_t slot, std::int32_t sound) = 0;
  virtual void stop(std::int64_t voice) = 0;
};

// The battle's effect scripts, run as the original runs them once per battle frame
// (battle_effect.c: BTL_startEFE, BTL_tickEFEEngine, BTL_renderEFEEngine, BTL_runEFEScript) over
// each slot's own copy of its data. The sub-ops do the original's maths and read the fighters
// through EffectBodies; their sounds go to EffectSounds and what the render scripts draw to
// draws(). The hit checks answer from the battle's own hits (noteHit) instead of the original's
// collision boxes. Frames, spawns, branches and calls follow the original, so each sound plays and
// each sprite moves on the frame the original's does.
class EffectRunner {
public:
  explicit EffectRunner(EffectSounds& sounds, std::uint32_t seed = 0);

  // The fighters the effects read; without them the effects start at the world's origin.
  void setBodies(const EffectBodies& bodies) { bodies_ = &bodies; }
  // The scripts' rand() draws (load_random, get_scattered_spawn_position, select_random_target)
  // come from `rolls` from now on, which must outlive this, instead of the runner's own sequence:
  // with the battle's rolls from the original's generator (Battle::originalRolls) they take the
  // original's numbers in the original's order (the effects run after the fighters each frame,
  // BTL_battleTickFrame: tickObjects). Nothing goes back to the runner's own sequence.
  void useRolls(ruleset::Random* rolls) { rolls_ = rolls; }
  // How much of the screen the battle shows, in the original's pixels (at least its 320 x 240): a
  // tiled screen layer (render_scrolling_background) repeats its tiles out to these edges, which
  // a wider window shows past the original's.
  void setScreenSize(EffectScreenPoint size) { screenSize_ = size; }

  // Loads a technique's effect for the battle and runs its init script (battle_effect.c:
  // BTL_loadMoveEFE, BTL_loadNextEFEFile), returning its slot.
  std::size_t load(assets::EffectScript script);
  [[nodiscard]] std::size_t slotCount() const { return slots_.size(); }

  // Runs a slot's start script for an attack of `source` at `target` (BTL_startEFE; the first
  // spawn takes the battle's attacker and its target). Returns the first instance
  // it spawned, which stop() takes, or nothing.
  std::optional<std::size_t> start(std::size_t slot, std::optional<FighterId> source = std::nullopt,
                                   std::optional<FighterId> target = std::nullopt);
  // Ends a running effect early (battle_main.c: BTL_removeMoveEffect, BTL_stopEFESubEffect).
  void stop(std::size_t slot, std::size_t section);
  // One battle frame: every running sub-effect's tick script, then the held sounds of the
  // sub-effects that ended (BTL_tickEFEEngine).
  void tick();
  // The battle hit with the attack the slot plays: the slot's next hit check reports it.
  void noteHit(std::size_t slot);
  // Stops every held sound (battle_effect.c: BTL_removeEFEEngine, BTL_stopEFESounds).
  void stopAll();
  // Runs every running sub-effect's render script (BTL_renderEFEEngine) and keeps what they drew
  // for draws(), until the next render.
  void render();
  [[nodiscard]] const std::vector<EffectDraw>& draws() const { return draws_; }

  // Whether a sub-effect of the slot still runs.
  [[nodiscard]] bool running(std::size_t slot) const;

  // The hit checks test the effects' instances against the fighters' bodies, as the original's do
  // (aabb.c: findAABBHitEntity; battle_effect.c: BTL_applyRadiusAttackHit, BTL_applyLineAttackHit,
  // BTL_checkTargetCollision), instead of answering from the battle's hits; every fighter they or
  // hit_target hit is kept in touched(), in order. For the battle's flight probe (flights.hpp).
  void touchBodies(bool on) { touchBodies_ = on; }
  [[nodiscard]] const std::vector<EffectTouch>& touched() const { return touched_; }
  // How many times the scripts turned their source to its target (battle_effect.c:
  // BTL_faceTargetEntity); the battle turns the fighter itself, on the frames the probe saw it.
  [[nodiscard]] std::size_t targetFacings() const { return targetFacings_; }

private:
  // A sub-effect of a slot (battle_effect.c: EfeSubEffect): the script it runs each frame and the
  // instance it runs on, as offsets into the slot.
  struct SubEffect {
    std::optional<std::uint32_t> tick;
    std::optional<std::uint32_t> render;
    std::optional<std::uint32_t> instance;
    std::optional<std::uint32_t> parent;
    // EfeSubEffect sourceEntity, targetEntity and boneOffset.
    std::optional<FighterId> source;
    std::optional<FighterId> target;
    std::optional<assets::EffectBoneOffset> boneOffset;
  };

  // An object's UV animation (battle_effect.c: EfeUvAnim): its keyframes' offset, the keyframe and
  // its frame, the frame of the whole loop, and how far it moved the object's texels.
  struct UvAnim {
    std::optional<std::uint32_t> keyframes;
    std::int32_t count = 0;
    std::int32_t keyframe = 0;
    std::int32_t keyframeFrame = 0;
    std::int32_t frame = 0;
    std::int32_t loopFrames = 0;
    std::int32_t u = 0;
    std::int32_t v = 0;
  };

  struct Slot {
    assets::EffectScript script;
    std::vector<std::byte> data;
    std::vector<SubEffect> subEffects;
    std::vector<UvAnim> uvAnims;
    std::vector<std::optional<EffectVector>> objectColors;
    bool hitPending = false;
    bool hitSinceStart = false;
    // The fighters hit_target marked since the start (the targets' isHit flags).
    std::vector<FighterId> hitTargets;
    // Its init script failed (EFE_ACTIVE_SECTION -2): it never starts.
    bool failed = false;
  };

  // A particle of an emitter (battle_effect.c: EfeParticle): how far it still is from where it
  // ends, its speed, its direction scaled to the emitter's start offset and where it set off, in
  // the original's 16-bit fields.
  struct Particle {
    std::int16_t distance = 0;
    std::int16_t velocity = 0;
    EffectVector direction;
    EffectVector position;
  };

  // battle_effect.c: EfeParticleEffect: the instance it follows, its frames left, its kind and
  // colour bytes, and its particles (the last stays empty: the search for a free one stops there).
  struct Emitter {
    std::size_t slot = 0;
    std::uint32_t instance = 0;
    std::int16_t frames = 0;
    std::int16_t startOffset = 0;
    std::int16_t startVelocity = 0;
    std::int16_t acceleration = 0;
    EffectVector color;
    std::int8_t kind = 0;
    std::array<Particle, 21> particles{};
  };

  // efe.c: CloudFXEntry: its frame and ground point, and the slot that made it.
  struct Cloud {
    std::size_t slot = 0;
    std::int32_t state = 0;
    std::int32_t x = 0;
    std::int32_t z = 0;
  };

  // efe.c: EfeFlashData, and the slot that made it.
  struct Flash {
    std::size_t slot = 0;
    EffectVector position;
    std::int32_t progress = 0;
    std::int32_t frames = 0;
    std::int32_t mode = 0;
    std::int32_t fixedDepth = 0;
    EffectVector colorStart;
    EffectVector colorEnd;
    std::int32_t sizeStart = 0;
    std::int32_t sizeEnd = 0;
  };

  struct CallFrame {
    std::size_t returnTo = 0; // an instruction index
    std::size_t subEffect = 0;
  };

  // A held sound (battle_effect.c: EFE_SOUND_DATA): its voice and the sub-effect that owns it.
  struct HeldSound {
    std::int64_t voice = 0;
    std::size_t slot = 0;
    std::size_t subEffect = 0;
  };

  void run(std::size_t slot, std::uint32_t script);
  std::optional<std::size_t> step(Slot& slot, std::size_t at);
  std::optional<std::size_t> returnFrom();
  std::optional<std::size_t> spawn(Slot& slot, const assets::EffectInstruction& in,
                                   std::size_t next);
  void subOp(Slot& slot, const assets::EffectInstruction& in);
  // The sub-ops by kind (effect_runner_subops.cpp); false for one that is not theirs.
  bool transformOp(Slot& slot, std::string_view name, const std::vector<std::int32_t>& args);
  bool vectorOp(Slot& slot, std::string_view name, const std::vector<std::int32_t>& args);
  bool drawOp(Slot& slot, std::string_view name, const std::vector<std::int32_t>& args);
  bool fighterOp(Slot& slot, std::string_view name, const std::vector<std::int32_t>& args);
  // The hit checks under touchBodies (effect_runner_subops.cpp); false for one that is not theirs.
  bool touchOp(Slot& slot, std::string_view name, const std::vector<std::int32_t>& args);
  void homeTowardPoint(Slot& slot, const std::vector<std::int32_t>& args);
  void selectTarget(Slot& slot, bool random, std::int32_t argument);
  void tickUvAnims(Slot& slot);
  // The particle emitters, clouds and flashes (effect_runner_fx.cpp): what their sub-ops add, one
  // frame of them after the scripts (BTL_tickEFEEngine: BTL_tickParticleEmitters; the clouds and
  // flashes as the world's objects that follow), and their draws after the render scripts.
  bool fxOp(Slot& slot, std::string_view name, const std::vector<std::int32_t>& args);
  // The layers drawn on the screen (effect_runner_screen.cpp).
  bool screenOp(Slot& slot, std::string_view name, const std::vector<std::int32_t>& args);
  void scrollingBackground(Slot& slot, const std::vector<std::int32_t>& args);
  void parallaxSprites(Slot& slot, const std::vector<std::int32_t>& args);
  void tickFx();
  void renderFx();
  void playSound(std::int32_t sound);
  // One draw of the original's rand(): 0 to 0x7FFF.
  std::uint32_t randomDraw();
  // Ends the running script: the sub-op found nothing to run on (EFE_SCRIPT_PTR = NULL).
  void abort() { aborted_ = true; }
  [[nodiscard]] SubEffect& context(Slot& slot) { return slot.subEffects[subEffect_]; }
  [[nodiscard]] std::optional<EffectBody> bodyOf(std::optional<FighterId> fighter) const;
  [[nodiscard]] std::optional<EffectMatrix> nodeOf(std::optional<FighterId> fighter,
                                                   std::int32_t index) const;
  // The running instance's transform (EfeInstance.transform, from offset 4).
  [[nodiscard]] EffectVector vectorAt(const Slot& slot, std::int32_t address) const;
  // The running instance's place (EfeInstance transform position, instance+4).
  [[nodiscard]] EffectVector instancePosition(const Slot& slot) const;
  void setVector(Slot& slot, std::int32_t address, const EffectVector& value) const;
  [[nodiscard]] std::int32_t instanceAddress(std::int32_t offset) const;
  static void writeHit(Slot& slot, std::int32_t address, bool consume);

  [[nodiscard]] std::int32_t address(const assets::EffectVariable& variable) const;
  [[nodiscard]] static std::int32_t read(const Slot& slot, std::int32_t address,
                                         assets::EffectWidth width);
  static void write(Slot& slot, std::int32_t address, assets::EffectWidth width,
                    std::int32_t value);
  [[nodiscard]] static std::int32_t frameOf(const Slot& slot,
                                            std::optional<std::uint32_t> instance);
  static void setFrame(Slot& slot, std::optional<std::uint32_t> instance, std::int32_t frame);

  EffectSounds* sounds_;
  const EffectBodies* bodies_ = nullptr;
  std::minstd_rand random_;
  ruleset::Random* rolls_ = nullptr;
  std::vector<Slot> slots_;
  std::vector<std::optional<HeldSound>> held_;
  std::vector<EffectDraw> draws_;
  // BTL_initializeParticleEmitters, initializeCloudFXData and initializeFlashData: 4 emitters, 60
  // clouds and 12 flashes.
  std::array<std::optional<Emitter>, 4> emitters_;
  std::array<std::optional<Cloud>, 60> clouds_;
  std::array<std::optional<Flash>, 12> flashes_;
  EffectScreenPoint screenSize_ = OriginalScreen;

  // The interpreter's registers (battle_effect.c: EFE_SCRIPT_*), valid while a script runs.
  std::size_t slot_ = 0;
  std::size_t subEffect_ = 0;
  std::optional<std::uint32_t> current_;
  std::optional<std::uint32_t> previous_;
  std::optional<std::size_t> activeSection_;
  std::int32_t register_ = 0;
  std::vector<std::int32_t> stack_;
  std::vector<CallFrame> calls_;
  bool aborted_ = false;
  // The fighter the start's first spawn takes as source and target, and the entity cursors of
  // select_next_target and the hit checks (EFE_TARGET_ENTITY_INDEX).
  std::optional<FighterId> startSource_;
  std::optional<FighterId> startTarget_;
  std::size_t targetCursor_ = 0;
  bool touchBodies_ = false;
  std::vector<EffectTouch> touched_;
  std::size_t targetFacings_ = 0;
};

} // namespace dwe::game::battle
