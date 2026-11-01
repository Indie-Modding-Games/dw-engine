#pragma once

#include <dwe/assets/clip_motion.hpp>
#include <dwe/assets/map_document.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace dwe::game::field {

// A ground point in the original's axes and units (x as its memory holds it, z the other way from
// the world's).
struct OriginalPoint {
  std::int32_t x = 0;
  std::int32_t z = 0;

  friend bool operator==(const OriginalPoint&, const OriginalPoint&) = default;
};

// The partner's clips by the original's ids (partner.c: setPartnerIdle, setPartnerSlowWalking,
// partnerTickWalking).
inline constexpr std::int32_t PartnerIdleClip = 0;
inline constexpr std::int32_t PartnerIdleTiredClip = 1;
inline constexpr std::int32_t PartnerWalkClip = 2;
inline constexpr std::int32_t PartnerWalkTiredClip = 3;
inline constexpr std::int32_t PartnerRunClip = 4;

// What the partner's state says about how it stands and walks (partner.c).
struct PartnerMood {
  // setPartnerIdle, setPartnerSlowWalking: sleepy, tired, injured or sick, it stands and walks
  // tired.
  bool tired = false;
  // partnerTickWalking: unhappy (condition 0x10), it walks instead of running.
  bool unhappy = false;
  // updateConditionAnimation: the gesture of its first condition or its mood; nothing when it
  // just stands (setPartnerIdle).
  std::optional<std::int32_t> gesture;
};

// The clip the partner plays, and how many times one started, so the drawing side restarts it
// when the same clip starts again.
struct PartnerClip {
  std::int32_t id = PartnerIdleClip;
  std::uint32_t starts = 0;

  friend bool operator==(const PartnerClip&, const PartnerClip&) = default;
};

// The partner following the player on the field, frame by frame and in the original's integers
// (map.c: partnerTickCollision, partner.c: partnerTickWalking, anim.c: tickAnimation): it looks
// at the player, or at the oldest cell of its trail while the player is out of its sight, turns
// back when its next step would touch the player, picks its clip by its distance to the player,
// and moves by its clip's root motion along its facing (applyRootMomentum), carrying the fraction
// of a unit as the original does.
class PartnerFollow {
public:
  // What one frame looks at.
  struct Frame {
    const assets::MapCollision* map = nullptr;
    // The player where its own frame left it (the original ticks the player first).
    OriginalPoint player;
    std::int32_t playerRadius = 100;
    // Whether a step from `from` to `to` runs into an NPC (entityCheckEntityCollision on the
    // others).
    std::function<bool(OriginalPoint from, OriginalPoint to)> npcBlocks;
    PartnerMood mood;
    // randomLimit(n): a number from 0 to n - 1.
    std::function<std::int32_t(std::int32_t)> randomBelow;
  };

  // `radius` is the partner's (its species' radius in the original's table); `clips` its model's
  // clips by original id. A clip it lacks stands still for a frame.
  explicit PartnerFollow(std::int32_t radius = 120, assets::ClipMotions clips = {});

  void setClips(assets::ClipMotions clips) { clips_ = std::move(clips); }
  void setRadius(std::int32_t radius) { radius_ = radius; }
  [[nodiscard]] std::int32_t radius() const { return radius_; }
  // partner.c: EMOTION_ANIM_TIMEOUT: once it stops it stands from 1 to `plays` plays of its idle
  // clip before its gesture (randomLimit(5) + 1).
  void setGestureWait(std::int32_t plays) { gesturePlays_ = plays; }

  // Puts the partner at `at` facing `rotation` (4096 a turn), on a whole unit, standing (as a
  // script or a map change puts it: startAnimation takes the location as it is).
  void place(OriginalPoint at, std::int32_t rotation);
  // Starts `clip` on its frame `frame` (0 the frame it starts) with `fraction` of a unit
  // (1/32768ths) carried on each axis: a run that starts where a trace of the original starts.
  void playFrom(std::int32_t clip, std::size_t frame, OriginalPoint fraction = {});
  // map.c: initializePartnerWaypoint: no trail, and the player's cell is the last one seen.
  void forgetTrail(OriginalPoint player);

  // One frame of the original (20 a second).
  void tick(const Frame& frame);
  // One battle frame, after the fighters moved (battle_main.c: BTL_tickPartnerWaypointTrail): when
  // the fighting partner changes cell out of the player's sight, the cell it left goes at the head
  // of the trail (the one it heads for first) unless the head is still in its own sight, so after
  // the fight it walks back the way it came. `startBattleTrail` takes its cell as the last one.
  void startBattleTrail(OriginalPoint partner) { battleCell_ = cellOf(partner); }
  void tickBattleTrail(const assets::MapCollision& map, OriginalPoint player,
                       OriginalPoint partner);

  [[nodiscard]] OriginalPoint position() const { return position_; }
  [[nodiscard]] std::int32_t rotation() const { return rotation_; }
  [[nodiscard]] PartnerClip clip() const { return {.id = clip_.id, .starts = clip_.starts}; }
  // The trail's cells, the oldest (the one it heads for) first.
  [[nodiscard]] std::vector<assets::CollisionCell> trail() const;

  // map.c: getModelTile: the original's cell of a point.
  [[nodiscard]] static assets::CollisionCell cellOf(OriginalPoint point);
  // The centre of a cell (entityLookAtTile, tickPartnerWaypoints).
  [[nodiscard]] static OriginalPoint centreOf(assets::CollisionCell cell);
  // partner.c: the partner's closeness to the player: 0 it runs, 1 it walks, 2 it stands.
  [[nodiscard]] std::int32_t closeness(OriginalPoint player) const;

private:
  // anim.c: what EntityAnim keeps of the clip under way.
  struct Playing {
    std::int32_t id = PartnerIdleClip;
    // The clip's frame whose step comes next.
    std::size_t frame = 0;
    // loopCount: the clip repeats forever (0xFF), ends with this pass (1) or has ended its loops
    // (0).
    enum class Loops : std::uint8_t { None, Forever, LastPass } loops = Loops::None;
    // animFlag bit 0: still running.
    bool running = true;
    // animFlag bit 1 (no step at all) and bits 3 to 6 (no step toward one side, 8 << rotation /
    // 1024), set by collisionGrace.
    std::uint8_t held = 0;
    // The step the next frame takes (the momentum the last frame's instructions left).
    assets::RootStep momentum;
    std::uint32_t starts = 0;
  };
  // What entityCheckCollision found ahead.
  enum class Blocker : std::uint8_t { None, Player, Npc, Map };

  void startClip(std::int32_t id);
  [[nodiscard]] const assets::ClipMotion* motionOf(std::int32_t id) const;
  [[nodiscard]] std::size_t clipLength(std::int32_t id) const;
  void tickClip();
  void tickTrail(const Frame& frame);
  void tickWalking(const Frame& frame);
  [[nodiscard]] Blocker checkStep(const Frame& frame, bool pastPlayer) const;
  void collisionGrace(const Frame& frame);
  [[nodiscard]] bool mapBlocks(const assets::MapCollision& map, OriginalPoint at,
                               OriginalPoint step) const;
  void setIdle(const PartnerMood& mood);
  void setSlowWalking(const PartnerMood& mood);
  void updateConditionAnimation(const PartnerMood& mood);
  void pickEmotionWait(const Frame& frame);

  std::int32_t radius_;
  assets::ClipMotions clips_;
  OriginalPoint position_;
  // anim.locX, anim.locZ: the position in 1/32768ths of a unit.
  std::int64_t locX_ = 0;
  std::int64_t locZ_ = 0;
  std::int32_t rotation_ = 0;
  Playing clip_;
  // PARTNER_ANIMATION: the clip partnerTickWalking wants.
  std::int32_t wanted_ = PartnerIdleClip;
  // IS_STANDING_STILL, STOP_DISTANCE_TIMER, EMOTION_ANIM_TIMEOUT.
  std::int32_t standing_ = 0;
  std::int32_t stopFrames_ = 0;
  std::int32_t emotionPlays_ = -1;
  std::int32_t gesturePlays_ = 5;
  // PARTNER_WAYPOINT_X/Y, _CURRENT and _COUNT, and the player's cell last seen.
  static constexpr std::int32_t TrailCapacity = 30;
  std::array<assets::CollisionCell, TrailCapacity> trail_{};
  std::int32_t trailCurrent_ = 0;
  std::int32_t trailCount_ = 0;
  assets::CollisionCell playerCell_{};
  // PARTNER_PREVIOUS_TILE: the partner's cell in the last battle frame.
  assets::CollisionCell battleCell_{};
};

} // namespace dwe::game::field
