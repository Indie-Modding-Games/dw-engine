#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// The final boss's arrival on Mt. Infinity's top (the EAB overlay, eab.c), counted in frames of the
// original. Story routine 40 (EAB_startBuildup) takes the boss off the map and starts the charge:
// a ring of ETCHI\BOSS_EFE rises round where it stands every 6 frames. The partner's routine 8
// (tamer.c: tamerTickMachinedramonSpawn, EAB_tick) then plays the arrival: the screen goes black,
// the boss shows alone on black with the camera going round it, rings and light squares spread
// from its feet, and after 340 frames everything comes back and the script goes on.
//
// The rings and the particles share their slots as the original's objects do; within a frame they
// run in the order of its object table: the charge, the arrival, the rings, the particles.
namespace dwe::game::field {

// The original's lerp (effect_common.c), with its wrap-around past t1.
[[nodiscard]] std::int32_t effectLerp(std::int32_t start, std::int32_t end, std::int32_t t0,
                                      std::int32_t t1, std::int32_t t);
// math.c: _sin: 512 a turn, 4096 for 1.
[[nodiscard]] std::int32_t sin512(std::int32_t angle);

// Which of the two ring kinds a slot holds (EAB_addBuildupRing, EAB_addSpawnRing).
enum class BossRingKind : std::uint8_t { Buildup, Spawn };

struct BossRing {
  BossRingKind kind = BossRingKind::Buildup;
  // Frames since it was added (EabModelFX.timer).
  std::int32_t timer = 0;
};

// The ring's size, 4096 for the model's own: x and z share one (EAB_renderBuildupRing,
// EAB_renderSpawnRing).
struct BossRingScale {
  std::int32_t across = 0;
  std::int32_t high = 0;
};
[[nodiscard]] BossRingScale bossRingScale(const BossRing& ring);

// EAB_addParticle: a square of four lines round the point where the boss stood when it was added,
// growing and fading to black over its life.
struct BossParticle {
  glm::vec3 point{0.0F};
  std::array<std::int32_t, 3> rgb{};
  std::int32_t timer = 0;
};
// EAB_renderParticle: half the square's side, in the original's units.
[[nodiscard]] std::int32_t bossParticleHalfSide(const BossParticle& particle);
// Its colour this frame, each channel 0 to 255.
[[nodiscard]] std::array<std::int32_t, 3> bossParticleColour(const BossParticle& particle);

// What a step asks of the boss and of the sounds. Sounds are in slot 8 (the map's bank, the boss
// bank routine 51 loaded).
struct BossArrivalCues {
  // startAnimation on the boss.
  std::optional<std::int32_t> animation;
  // stopSound: the sounds playing stop (the charge's hum, the arrival's roar).
  bool stopSounds = false;
  // playSound2(8, n), a sound held to be stopped; playSound(8, n).
  std::optional<std::int32_t> heldSound;
  std::optional<std::int32_t> sound;
  // The slot the boss's own sounds (its clips' bank 4) play from from now on.
  std::optional<std::int32_t> bossSoundSlot;
};

// The camera of the arrival (EAB_calculateCameraOrbit): it looks at the boss's root bone 100 up,
// from `distance` away at `angle` (4096 a turn) plus the boss's facing, raised by `height`.
struct BossOrbit {
  std::int32_t angle = 0;
  std::int32_t distance = 0;
  std::int32_t height = 0;
};

class BossArrival {
public:
  // EAB_addBuildupRing and EAB_addSpawnRing look at 3 slots; EAB_addParticle at 100.
  static constexpr std::size_t RingSlots = 3;
  static constexpr std::size_t ParticleSlots = 100;
  // ENTITY1_WIREFRAME_TOTAL: 16 shares of 16 drawn solid, none as a wireframe.
  static constexpr std::int32_t SolidShares = 16;

  // Story routine 40: EAB_startBuildup. Takes the boss off the map and starts the charge.
  BossArrivalCues startBuildup();
  // The partner's routine 8: EAB_tick(entity, 0). The first frame of the arrival is the next one.
  BossArrivalCues startArrival();
  // One frame of the original: `bossPoint` is where the boss stands (its posData location).
  BossArrivalCues frame(glm::vec3 bossPoint);
  // A map change takes everything away.
  void clear() { *this = BossArrival{}; }

  [[nodiscard]] bool charging() const { return charging_; }
  [[nodiscard]] bool arriving() const { return arrivalFrame_.has_value(); }
  // The arrival ran its 340 frames: the script's wait is over (SOME_SCRIPT_SYNC_BIT).
  [[nodiscard]] bool arrivalOver() const { return arrivalOver_; }
  // The arrival's frame (EabState.frame), when it runs.
  [[nodiscard]] std::optional<std::int32_t> arrivalFrame() const { return arrivalFrame_; }

  // The boss is off the map (isOnMap = 0) from routine 40 until the arrival's frame 50.
  [[nodiscard]] bool bossHidden() const { return bossHidden_; }
  // Frames 50 to 319: the map and every other character are not drawn (setMapLayerEnabled(0),
  // EAB_setEntitiesVisible(0)): the boss alone on black.
  [[nodiscard]] bool othersHidden() const { return othersHidden_; }
  // EAB_renderFlash: how much is subtracted from the whole screen this frame, 0 to 255 (255 from
  // frame 45, back to 0 by frame 83); nothing when it does not draw.
  [[nodiscard]] std::optional<std::int32_t> blackout() const { return blackout_; }
  // The arrival's camera this frame (frames 50 to 318); nothing leaves the field's.
  [[nodiscard]] std::optional<BossOrbit> orbit() const { return orbit_; }
  // How many of 16 shares of the boss are drawn solid (renderWireframed draws the rest as lines).
  [[nodiscard]] std::int32_t solidShares() const { return solidShares_; }

  [[nodiscard]] const std::array<std::optional<BossRing>, RingSlots>& rings() const {
    return rings_;
  }
  [[nodiscard]] const std::vector<BossParticle>& particles() const { return particles_; }

private:
  void tickBuildup();
  void tickArrival(glm::vec3 bossPoint, BossArrivalCues& cues);
  void addRing(BossRingKind kind);
  void addParticle(glm::vec3 point);
  void tickRings();
  void tickParticles();
  void orbitAt(std::int32_t frame);

  bool charging_ = false;
  // MAIN_D_801353F0.frame: the charge's frame, 0 to 17.
  std::int32_t buildupFrame_ = 0;
  std::optional<std::int32_t> arrivalFrame_;
  std::int32_t phase_ = 0;
  bool arrivalOver_ = false;
  bool bossHidden_ = false;
  bool othersHidden_ = false;
  std::optional<std::int32_t> blackout_;
  std::optional<BossOrbit> orbit_;
  std::int32_t solidShares_ = SolidShares;
  std::array<std::optional<BossRing>, RingSlots> rings_{};
  std::vector<BossParticle> particles_;
};

} // namespace dwe::game::field
