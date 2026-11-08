#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/field/grid.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

#include "messages.hpp"

namespace dwe::game::field {

namespace {

using assets::CollisionCell;
using scene::AnimationIntent;
using scene::Motion;
using scene::Transform;

constexpr float Turn = 2.0F * std::numbers::pi_v<float>;
// A move that makes less than this fraction of its distance counts as stuck.
constexpr float StuckFraction = 0.1F;
// A care walk to a point is there this close to it (partner.c: tickEntityWalkTo ends on it).
constexpr float CareArrival = 1.0F;
// Click-to-move gives up on a waypoint after pushing against a wall this long.
constexpr float PathStallSeconds = 0.5F;
// How many cells around a clicked cell to look for one the player can get to.
constexpr int ClickSearchCells = 3;
// The directions a partner making room tries, in turns from straight away from the player.
constexpr std::array<float, 5> RoomTurns = {0.0F, 0.125F, -0.125F, 0.25F, -0.25F};
// A partner nearer the camera than the player hides it unless this fraction of the two's touching
// distance to a side of the camera's line to the player.
constexpr float HiddenFraction = 0.75F;
// tamer.c: getMapRotation rounds the camera's heading to 512 of the original's 4096: an eighth of a
// turn.
constexpr float CameraHeadingStep = Turn / 8.0F;
// The ground directions at each eighth of a turn from +X toward +Z, exact so that straight moves
// stay on their line.
constexpr float Diagonal = std::numbers::sqrt2_v<float> / 2.0F;
constexpr std::array<glm::vec3, 8> HeadingDirections{{{1.0F, 0.0F, 0.0F},
                                                      {Diagonal, 0.0F, Diagonal},
                                                      {0.0F, 0.0F, 1.0F},
                                                      {-Diagonal, 0.0F, Diagonal},
                                                      {-1.0F, 0.0F, 0.0F},
                                                      {-Diagonal, 0.0F, -Diagonal},
                                                      {0.0F, 0.0F, -1.0F},
                                                      {Diagonal, 0.0F, -Diagonal}}};

glm::vec2 ground(glm::vec3 v) {
  return {v.x, v.z};
}

float groundDistance(glm::vec3 a, glm::vec3 b) {
  return glm::length(ground(a) - ground(b));
}

// Turns `from` toward `to` by at most `step` radians (any step when step <= 0), the short way.
float turnToward(float from, float to, float step) {
  const float delta = std::remainder(to - from, Turn);
  if (step <= 0.0F || std::abs(delta) <= step) {
    return std::remainder(from + delta, Turn);
  }
  return std::remainder(from + std::copysign(step, delta), Turn);
}

// tamer.c: loadMapEntities puts the partner 200 units toward the cardinal direction behind the
// player, in the original's axes, by bands of the player's rotation.
glm::ivec3 partnerSpawnOffset(std::int32_t rotation, std::int32_t distance) {
  rotation = ((rotation % 4096) + 4096) % 4096;
  if (rotation <= 0x200 || rotation > 0xE00) {
    return {0, 0, distance};
  }
  if (rotation <= 0x600) {
    return {distance, 0, 0};
  }
  if (rotation <= 0xA00) {
    return {0, 0, -distance};
  }
  return {-distance, 0, 0};
}

// map.c: COLLISION_GRACE_ROTATION: the cardinal rotations collisionGrace tries, by the eighth of a
// turn the character faces (rotation / 512), in the original's units.
constexpr std::array<std::array<std::int32_t, 4>, 8> GraceRotations{{{0x000, 0x400, 0xC00, 0x800},
                                                                     {0x400, 0x000, 0x800, 0xC00},
                                                                     {0x400, 0x800, 0x000, 0xC00},
                                                                     {0x800, 0x400, 0xC00, 0x000},
                                                                     {0x800, 0xC00, 0x400, 0x000},
                                                                     {0xC00, 0x800, 0x000, 0x400},
                                                                     {0xC00, 0x000, 0x800, 0x400},
                                                                     {0x000, 0xC00, 0x400, 0x800}}};
// map.c: collisionGrace turns at most 0x50 a frame toward the free side, and 32 with none free.
constexpr std::int32_t GraceTurn = 0x50;
constexpr std::int32_t StuckTurn = 32;

std::int32_t wrapRotation(std::int32_t rotation) {
  return ((rotation % core::OriginalAnglePerTurn) + core::OriginalAnglePerTurn) %
         core::OriginalAnglePerTurn;
}

// A step's components too short to move the original's integer position are none (map.c:
// entityCheckCollision zeroes the cross component of a cardinal step the same way).
glm::vec2 still(glm::vec2 step) {
  constexpr float Still = 0.5F;
  return {std::abs(step.x) < Still ? 0.0F : step.x, std::abs(step.y) < Still ? 0.0F : step.y};
}

// map.c: collisionGrace: the four cardinal rotations in COLLISION_GRACE_ROTATION's order for the
// eighth of a turn the character faces; the first one whose step ahead is free (`blocked` says
// whether a step collides) turns it toward that one, 0x50 a frame at most, and the blocked ones
// before it cancel the step's part toward them (animFlag 8 << rotation / 1024, anim.c:
// applyRootMomentum). With none free it turns 32 and stands. The step is `distance` along the new
// facing, less the cancelled parts.
struct Grace {
  float yaw = 0.0F;
  glm::vec2 step{};
};

Grace collisionGrace(float yaw, float distance, const std::function<bool(glm::vec2)>& blocked) {
  const std::int32_t from = assets::yawToOriginal(yaw);
  std::vector<glm::vec2> sides;
  for (const std::int32_t rotation : GraceRotations.at(static_cast<std::size_t>(from / 512))) {
    const glm::vec2 cardinal =
        glm::round(ground(assets::forwardOfYaw(assets::yawFromOriginal(rotation))));
    if (blocked(cardinal * distance)) {
      sides.push_back(cardinal);
      continue;
    }
    const std::int32_t clockwise = wrapRotation(from - rotation);
    const std::int32_t counter = wrapRotation(rotation - from);
    const std::int32_t turned = clockwise < counter ? from - std::min(clockwise, GraceTurn)
                                                    : from + std::min(counter, GraceTurn);
    const float turnedYaw = assets::yawFromOriginal(wrapRotation(turned));
    glm::vec2 step = still(ground(assets::forwardOfYaw(turnedYaw)) * distance);
    for (const glm::vec2 side : sides) {
      const float toward = glm::dot(step, side);
      if (toward > 0.0F) {
        step -= side * toward;
      }
    }
    return {.yaw = turnedYaw, .step = step};
  }
  return {.yaw = assets::yawFromOriginal(wrapRotation(from + StuckTurn)), .step = {}};
}

// The original's ground point of a world position, and back (core::fromOriginalAxes).
OriginalPoint originalPointOf(glm::vec3 world) {
  return {.x = static_cast<std::int32_t>(std::lround(world.x)),
          .z = static_cast<std::int32_t>(std::lround(-world.z))};
}

glm::vec3 worldPointOf(OriginalPoint point, float height) {
  return {static_cast<float>(point.x), height, -static_cast<float>(point.z)};
}

// The partner's clips without its model's: it stands a second at a time, and walks and runs
// straight ahead at its rules' speeds, a step a frame.
assets::ClipMotions defaultPartnerClips(const MoverRules& mover, float frameSeconds) {
  const auto walking = [&](float speed) {
    const auto step = static_cast<std::int32_t>(std::lround(speed * frameSeconds));
    return assets::ClipMotion{.steps = {{}, {.side = 0, .forward = step}}, .repeatFrom = 1};
  };
  const auto standFrames = static_cast<std::size_t>(std::lround(1.0F / frameSeconds));
  const assets::ClipMotion standing{.steps = std::vector<assets::RootStep>(standFrames),
                                    .repeatFrom = 1};
  return {{PartnerIdleClip, standing},
          {PartnerIdleTiredClip, standing},
          {PartnerWalkClip, walking(mover.walkSpeed)},
          {PartnerWalkTiredClip, walking(mover.walkSpeed)},
          {PartnerRunClip, walking(mover.runSpeed)}};
}

// The motion the field reports for a clip of the partner.
Motion motionOfClip(std::int32_t clip) {
  if (clip == PartnerRunClip) {
    return Motion::Run;
  }
  return clip == PartnerWalkClip || clip == PartnerWalkTiredClip ? Motion::Walk : Motion::Idle;
}

} // namespace

Field::Field(scene::World& world, FieldRules rules, ScriptWarpSetup scriptWarps)
    : world_(&world), rules_(std::move(rules)), scriptWarps_(std::move(scriptWarps.warps)),
      facts_(std::move(scriptWarps.facts)), writeTrigger_(std::move(scriptWarps.writeTrigger)),
      unknownPolicy_(scriptWarps.unknownPolicy), explore_(scriptWarps.explore),
      hasMap_(std::move(scriptWarps.hasMap)), player_(world.create()), partner_(world.create()),
      npcs_(world, rules_.npc, rules_.transition.frameSeconds),
      follow_(static_cast<std::int32_t>(std::lround(rules_.partner.mover.radius)),
              defaultPartnerClips(rules_.partner.mover, rules_.transition.frameSeconds)) {
  follow_.setGestureWait(rules_.partner.gestureIdlePlays);
  entt::registry& registry = world_->registry();
  registry.emplace<Transform>(player_);
  registry.emplace<scene::ModelRef>(player_, rules_.player.model);
  registry.emplace<AnimationIntent>(player_);
  registry.emplace<scene::PlayerTag>(player_);
  registry.emplace<Transform>(partner_);
  registry.emplace<scene::ModelRef>(partner_, rules_.partner.mover.model);
  registry.emplace<AnimationIntent>(partner_);
  registry.emplace<scene::PartnerTag>(partner_);
}

core::Result<void> Field::enterMap(const assets::MapDocument& map, std::int32_t exit) {
  const assets::MapWarp* warp = map.warp(exit);
  if (warp == nullptr) {
    return core::fail(std::format("map '{}' has no exit {}", map.id, exit));
  }
  const bool walked = leavingWalks_;
  auto queued = std::exchange(queuedChange_, std::nullopt);
  enterMapAt(map, assets::warpSpawn(*warp));
  shadeHeld_ = false;
  fadeInSeconds_ = 0.0F;
  fadeInFrames_ = rules_.transition.fadeInFrames;
  fadeInHolds_ = walked;
  leavingWalks_ = true;

  const glm::ivec3 offset = partnerSpawnOffset(
      warp->rotation, static_cast<std::int32_t>(std::lround(rules_.partner.spawnDistance)));
  const glm::vec3 position = assets::toWorldPosition(warp->spawn + offset);
  const assets::SpawnPoint spawn = assets::warpSpawn(*warp);
  const GroundSquare there{.centre = ground(position), .half = rules_.partner.mover.radius};
  const bool clearOfOthers = std::ranges::none_of(
      bodies(), [&](const Body& body) { return body.who != 1 && overlaps(there, squareOf(body)); });
  if (fitsOnGrid(collision_, position, rules_.partner.mover.radius) && clearOfOthers) {
    scene::place(world_->registry().get<Transform>(partner_), position,
                 assets::yawOfDirection(spawn.position - position));
  }
  if (queued) {
    startScriptedMapChange(std::move(queued->first), queued->second);
  }
  return {};
}

void Field::enterMapAt(const assets::MapDocument& map, assets::SpawnPoint spawn,
                       std::optional<assets::SpawnPoint> partner) {
  mapId_ = map.id;
  collision_ = map.collision;
  regions_ = map.regions;
  warps_ = map.warps;
  npcs_.enterMap(map.npcs);
  refreshScriptWarps();
  const glm::vec3 view =
      assets::toWorldPosition(map.camera.reference) - assets::toWorldPosition(map.camera.viewpoint);
  // tamer.c: tamerTickWalkingState adds the d-pad's direction to the camera's heading rounded to an
  // eighth of a turn (getMapRotation), so "up" goes the same way on every map with a camera turned
  // a little. The original rounds negative headings toward zero; this rounds to the nearest
  // (docs/reference/campo.md).
  const float heading =
      glm::length(ground(view)) > 0.0F ? std::atan2(view.z, view.x) : -Turn / 4.0F;
  const auto eighth = static_cast<std::size_t>((std::lround(heading / CameraHeadingStep) + 8) % 8);
  assert(eighth < HeadingDirections.size());
  cameraForward_ = HeadingDirections[eighth];
  cameraView_ =
      glm::length(ground(view)) > 0.0F ? glm::normalize(ground(view)) : ground(cameraForward_);

  entt::registry& registry = world_->registry();
  // tamer.c: setupTamerOnWarp: the player at the spawn, turned its way, standing (animation 0).
  scene::place(registry.get<Transform>(player_), spawn.position, spawn.yaw);
  registry.get<AnimationIntent>(player_) = {};
  registry.get<AnimationIntent>(partner_) = {};

  moveTarget_.reset();
  path_.clear();
  pathStallSeconds_ = 0.0F;
  leaving_.reset();
  leavingSeconds_ = 0.0F;
  pendingChange_.reset();
  waitingForMap_ = false;
  choice_.reset();
  choiceWarps_.clear();
  choiceVanilla_ = nullptr;
  changeTarget_.clear();
  fadeInSeconds_.reset();
  triggerSteps_.clear();
  // The original has no guard against warping again at once; its spawns are never on triggers.
  // Remembering the value under the spawn makes a trigger fire only when stepped onto.
  lastPlayerCell_ = assets::collisionCellAt(spawn.position);
  // map.c: loadMap starts the way back on the cell the player enters on (initializeTamerWaypoints).
  playerTrail_.enter(lastPlayerCell_);
  lastTrigger_ = collision_.trigger(assets::originalCellAt(spawn.position)).value_or(0);
  follow_.forgetTrail(originalPointOf(spawn.position));
  partnerStuckSeconds_ = 0.0F;
  partnerMakesRoom_ = false;
  freeTick_ = false;
  playerGlide_.on = false;
  partnerGlide_.on = false;
  // main.c: initializeLoadedMap puts the partner back where the save left it. The original moves an
  // NPC it lands on off the map (or both characters to the exit); dwe puts a partner whose spot is
  // taken or off the grid next to the player instead.
  if (!partner || !placePartnerAt(*partner)) {
    placePartnerNearPlayer();
  }
  partnerTeleports_ = 0;
  cameraRest_.reset();
  if (scriptHeld_) {
    scriptCamera_ = spawn.position;
  }
  releaseHandoff("map entry");
}

void Field::startPlay() {
  core::logDebug("[port:field] main (main.c:3494): play starts on {}: fadeFromBlack({}), the "
                 "player free",
                 mapId_, rules_.transition.startFadeInFrames);
  shadeHeld_ = false;
  fadeInSeconds_ = 0.0F;
  fadeInFrames_ = rules_.transition.startFadeInFrames;
  fadeInHolds_ = false;
}

void Field::holdShade(std::int32_t shade) {
  fadeInSeconds_.reset();
  fadeProgress_ = shade;
  shadeHeld_ = true;
}

void Field::releaseShade() {
  if (!std::exchange(shadeHeld_, false) || leaving_ || pendingChange_ || waitingForMap_) {
    return;
  }
  core::logDebug("[port:field] fadeFromBlack (fade.c): no map change follows the held fade, the "
                 "screen fades back in over {} frames",
                 rules_.transition.fadeInFrames);
  fadeInSeconds_ = 0.0F;
  fadeInFrames_ = rules_.transition.fadeInFrames;
  fadeInHolds_ = false;
}

void Field::setInput(const FieldInput& input) {
  input_ = input;
  if (input.cancelTarget) {
    dropMoveTarget();
  }
  // A click while something holds the player is not a walk order: it would walk him once let go.
  if (input.moveTarget && !mapChangePending() && !scriptHeld_ && !careHeld_ &&
      itemHold_ == ItemHold::None) {
    planPath(*input.moveTarget);
  }
}

void Field::dropMoveTarget() {
  if (moveTarget_ || !path_.empty()) {
    core::logDebug("[field] the click-to-move target is dropped");
  }
  moveTarget_.reset();
  path_.clear();
  pathStallSeconds_ = 0.0F;
}

void Field::tick(float seconds) {
  triggerSteps_.clear();
  cellSections_.clear();
  npcs_.clearContacts();
  gameTimeRan_ = false;
  entt::registry& registry = world_->registry();
  // Every character starts its tick here, the NPCs too: while a map change fades out or waits for
  // its map their own tick does not run, and a transform left as an older tick left it would draw
  // a move that is not happening.
  beginTicks(seconds);
  const bool wasFree = std::exchange(freeTick_, false);
  const bool wasFollowing = std::exchange(partnerFollowing_, false);
  registry.get<AnimationIntent>(partner_) = {};
  // The overlap watch looks at the ticks the player moves freely only; a hold starts it over.
  if (leaving_ || pendingChange_ || waitingForMap_ || choice_ || mapId_.empty() || scriptHeld_ ||
      careHeld_ || itemHold_ != ItemHold::None) {
    overlapWatch_.clear();
    // Held, the player has no click-to-move target to walk on to once let go.
    dropMoveTarget();
  }
  if (leaving_) {
    walkOut(seconds);
    return;
  }
  if (pendingChange_ || waitingForMap_ || choice_ || mapId_.empty()) {
    registry.get<AnimationIntent>(player_) = {};
    return;
  }
  if (npcs_.takeArrivals()) {
    requestSeparation("npc on the map or let go");
  }
  if (scriptHeld_) {
    // A press made while held is the script's, not the cell's.
    input_.confirm = false;
    tickScripted(seconds);
    return;
  }
  if (careHeld_) {
    input_.confirm = false;
    tickCare(seconds);
    return;
  }
  if (itemHold_ != ItemHold::None) {
    tickItemHold(seconds, wasFollowing);
    return;
  }
  // The release rule (releaseHandoff): after a script the partner first makes room on foot.
  if (separationDue_ && !partnerMakesRoom_) {
    separateActors(*std::exchange(separationDue_, std::nullopt));
  }
  if (fadeInSeconds_) {
    // tamer.c: tamerTickChangeMap gives the player back control on the fade in's last frame
    // (FADE_IN_CURRENT reaching 20); fade.c: renderFadeIn still draws that frame and ends on the
    // next.
    *fadeInSeconds_ += seconds;
    const std::int32_t frame = fadeFrame(*fadeInSeconds_);
    if (frame > fadeInFrames_) {
      endFadeIn();
    } else if (frame < fadeInFrames_ && fadeInHolds_) {
      dropMoveTarget();
      registry.get<AnimationIntent>(player_) = {};
      tickNpcs(seconds, false);
      overlapWatch_.clear();
      return;
    }
  }
  // clock.c: tickGameClock runs while the player plays.
  gameTimeRan_ = true;
  freeTick_ = true;
  if (!wasFree) {
    // Held, the characters played what the hold gave them: the next press starts a clip again.
    playerMotion_ = Motion::Idle;
    partnerIntent_ = {};
    frameClock_ = rules_.transition.frameSeconds;
  }
  const std::int32_t frames = takeFrames(seconds);
  // tamer.c: tamerTickWalkingState keeps the way back before it moves the player.
  playerTrail_.tick(assets::collisionCellAt(registry.get<Transform>(player_).position), collision_);
  movePlayer(frames);
  // tamer.c: checkMapInteraction: an NPC's section starts the script, so a confirm cell's section
  // that frame finds a script running and is dropped.
  if (touchNpcs()) {
    input_.confirm = false;
  }
  checkTriggers();
  checkConfirm();
  if (frames == 0) {
    partnerFollowing_ = wasFollowing;
  }
  movePartnerFrames(frames);
  tickNpcs(seconds, true);
  watchOverlaps(seconds);
}

bool Field::touchNpcs() {
  entt::registry& registry = world_->registry();
  const auto& transform = registry.get<Transform>(player_);
  const Motion motion = registry.get<AnimationIntent>(player_).motion;
  const glm::vec2 facing = ground(assets::forwardOfYaw(transform.yaw));
  // map.c: entityCheckCollision tests the player one frame of its animation's motion ahead, so
  // pushing against an NPC touches it.
  float speed = 0.0F;
  if (motion == Motion::Run) {
    speed = rules_.player.runSpeed;
  } else if (motion == Motion::Walk) {
    speed = rules_.player.walkSpeed;
  }
  const glm::vec2 ahead =
      ground(transform.position) + (facing * (speed * rules_.transition.frameSeconds));
  const std::size_t before = npcs_.contacts().size();
  npcs_.touchFromPlayer({.centre = ahead, .half = rules_.player.radius}, facing,
                        motion == Motion::Idle, input_.confirm, generousTalk_);
  return npcs_.contacts().size() > before;
}

void Field::tickNpcs(float seconds, bool playerFree) {
  entt::registry& registry = world_->registry();
  npcs_.tick(seconds, collision_,
             {.player = {.centre = ground(registry.get<Transform>(player_).position),
                         .half = rules_.player.radius},
              .partner = {.centre = ground(registry.get<Transform>(partner_).position),
                          .half = rules_.partner.mover.radius},
              .playerFree = playerFree});
  applyClipRootMotion(seconds);
}

void Field::walkOut(float seconds) {
  // tamer.c: tamerTickChangeMap plays the walk animation while the screen fades out and tests no
  // collision: the root motion carries the player on the way it faces.
  auto& transform = world_->registry().get<Transform>(player_);
  if (leavingWalks_) {
    transform.position += assets::forwardOfYaw(transform.yaw) * (rules_.player.walkSpeed * seconds);
    world_->registry().get<AnimationIntent>(player_) = {.motion = Motion::Walk, .speedScale = 1.0F};
  } else {
    world_->registry().get<AnimationIntent>(player_) = {};
  }
  leavingSeconds_ += seconds;
  // The new map loads when FADE_OUT_CURRENT reaches 20.
  if (fadeFrame(leavingSeconds_) >= rules_.transition.fadeOutFrames) {
    pendingChange_ = std::exchange(leaving_, std::nullopt);
  }
}

std::int32_t Field::fadeFrame(float seconds) const {
  // A little slack, so that ticks adding up to a frame's length land on that frame despite
  // rounding.
  constexpr float Slack = 1e-3F;
  return 1 +
         static_cast<std::int32_t>(std::floor((seconds / rules_.transition.frameSeconds) + Slack));
}

float Field::fadeLevel() const {
  const TransitionRules& transition = rules_.transition;
  if (leaving_) {
    return shadeDarkness(transition,
                         fadeOutShade(transition, fadeFrame(leavingSeconds_), leavingFrom_));
  }
  if (pendingChange_ || waitingForMap_) {
    // The frame that loads the new map already draws the fade in's first shade.
    return shadeDarkness(transition, fadeInShade(transition, 1));
  }
  if (fadeInSeconds_) {
    return shadeDarkness(transition,
                         fadeInShade(transition, fadeFrame(*fadeInSeconds_), fadeInFrames_));
  }
  if (shadeHeld_ && fadeProgress_) {
    return shadeDarkness(transition, *fadeProgress_);
  }
  return 0.0F;
}

std::optional<std::string_view> Field::mapNameShown() const {
  // tamer.c: tamerTickChangeMap adds the name on frame 10 of the fade out and changeMap's frame
  // removes it.
  const bool shown = leavingShowsName_ &&
                     ((leaving_ && fadeFrame(leavingSeconds_) >= rules_.transition.nameFrame) ||
                      pendingChange_ || waitingForMap_);
  if (!shown) {
    return std::nullopt;
  }
  return std::string_view{changeTarget_};
}

void Field::startScriptedMapChange(MapChange change, bool showName) {
  core::logDebug("[port:field] scriptTickChangeMap (map.c:5277): to {} exit {}, name {}, queued {}",
                 change.map, change.exit, showName, waitingForMap_);
  if (waitingForMap_) {
    queuedChange_ = std::pair{std::move(change), showName};
    return;
  }
  pendingChange_.reset();
  leave(std::move(change));
  leavingWalks_ = false;
  leavingShowsName_ = showName;
  fadeInSeconds_.reset();
  choice_.reset();
}

std::optional<MapChange> Field::takeMapChange() {
  if (!pendingChange_) {
    return std::nullopt;
  }
  waitingForMap_ = true;
  return std::exchange(pendingChange_, std::nullopt);
}

CharacterState Field::player() const {
  const auto& transform = world_->registry().get<Transform>(player_);
  return {.position = transform.position,
          .yaw = transform.yaw,
          .motion = world_->registry().get<AnimationIntent>(player_).motion};
}

void Field::startGlide(FrameGlide& glide, scene::Entity entity) const {
  const auto& transform = world_->registry().get<Transform>(entity);
  // From where it is drawn now, at the start of this tick: on a frame that starts early (a press
  // from standing) the last glide had not ended.
  glide.from = glidePose(glide, entity, 0.0F)
                   .value_or(scene::Pose{.position = transform.position, .yaw = transform.yaw});
  glide.seconds = 0.0F;
  glide.on = true;
}

void Field::endGlide(FrameGlide& glide, scene::Entity entity) const {
  const auto& transform = world_->registry().get<Transform>(entity);
  glide.to = {.position = transform.position, .yaw = transform.yaw};
}

std::optional<scene::Pose> Field::glidePose(const FrameGlide& glide, scene::Entity entity,
                                            float alpha) const {
  const auto& transform = world_->registry().get<Transform>(entity);
  if (!glide.on || !fieldRan_ || transform.position != glide.to.position ||
      transform.yaw != glide.to.yaw) {
    return std::nullopt;
  }
  const float frame = rules_.transition.frameSeconds;
  const float done = std::clamp((glide.seconds + (alpha * tickSeconds_)) / frame, 0.0F, 1.0F);
  const float turn = std::remainder(glide.to.yaw - glide.from.yaw, Turn);
  return scene::Pose{.position = glm::mix(glide.from.position, glide.to.position, done),
                     .yaw = glide.from.yaw + (turn * done)};
}

scene::Pose Field::drawnPose(scene::Entity entity, float alpha) const {
  const auto& transform = world_->registry().get<Transform>(entity);
  if (!smoothMotion_) {
    return {.position = transform.position, .yaw = transform.yaw};
  }
  std::optional<scene::Pose> pose;
  if (entity == player_) {
    pose = glidePose(playerGlide_, entity, alpha);
  } else if (entity == partner_) {
    pose = glidePose(partnerGlide_, entity, alpha);
  }
  return pose.value_or(scene::interpolate(transform, alpha));
}

void Field::setPartnerClips(assets::ClipMotions clips) {
  // A script's clip carries the partner by the same root motion (moveByClip).
  if (!clips.empty()) {
    world_->registry().emplace_or_replace<CharacterClipMotions>(
        partner_, std::make_shared<const assets::ClipMotions>(clips));
  }
  follow_.setClips(clips.empty()
                       ? defaultPartnerClips(rules_.partner.mover, rules_.transition.frameSeconds)
                       : std::move(clips));
}

std::optional<PartnerClip> Field::partnerFollowClip() const {
  if (!partnerFollowing_ && !scriptHeld_) {
    return std::nullopt;
  }
  return follow_.clip();
}

void Field::playPartnerFrom(std::int32_t clip, std::size_t frame, OriginalPoint fraction) {
  syncPartnerFollow();
  follow_.playFrom(clip, frame, fraction);
  partnerFollowing_ = true;
  world_->registry().get<AnimationIntent>(partner_) = {.motion = motionOfClip(clip),
                                                       .speedScale = 1.0F};
  partnerIntent_ = world_->registry().get<AnimationIntent>(partner_);
}

CharacterState Field::partner() const {
  const auto& transform = world_->registry().get<Transform>(partner_);
  return {.position = transform.position,
          .yaw = transform.yaw,
          .motion = world_->registry().get<AnimationIntent>(partner_).motion};
}

bool Field::warpsOn(std::int32_t trigger) const {
  if (const auto exit = rules_.triggers.exitOf(trigger)) {
    const auto warp = std::ranges::find(warps_, *exit, &assets::MapWarp::exit);
    if (warp != warps_.end() && warp->targetMapId) {
      return true;
    }
  }
  return std::ranges::contains(warpingStepCells_, trigger);
}

void Field::refreshScriptWarps() {
  warpingStepCells_.clear();
  for (const ScriptWarp& warp : scriptWarps_) {
    if (warp.map != mapId_) {
      continue;
    }
    if (warp.via != WarpVia::Step || std::ranges::contains(warpingStepCells_, warp.section)) {
      continue;
    }
    const bool warps =
        explore_
            ? !explorationWarps(scriptWarps_, mapId_, WarpVia::Step, warp.section, hasMap_).empty()
            : warpOnCell(scriptWarps_, mapId_, WarpVia::Step, warp.section, facts_, unknownPolicy_)
                      .warp != nullptr;
    if (warps) {
      warpingStepCells_.push_back(warp.section);
    }
  }
}

void Field::reloadData(FieldRules rules, std::vector<ScriptWarp> scriptWarps) {
  rules_ = std::move(rules);
  scriptWarps_ = std::move(scriptWarps);
  choice_.reset();
  choiceWarps_.clear();
  choiceVanilla_ = nullptr;
  undecidedReported_.clear();
  allowedReported_.clear();
  ambiguousReported_.clear();
  follow_.setGestureWait(rules_.partner.gestureIdlePlays);
  refreshScriptWarps();
}

void Field::writeTriggersOf(const ScriptWarp& warp) {
  if (!writeTrigger_ || warp.setsFlags.empty()) {
    return;
  }
  for (const FlagEffect& effect : warp.setsFlags) {
    writeTrigger_(effect.trigger, effect.set);
  }
  refreshScriptWarps();
}

std::optional<MapChange> Field::takeScriptWarp(WarpVia via, std::int32_t value) {
  const CellWarp cell = warpOnCell(scriptWarps_, mapId_, via, value, facts_, unknownPolicy_);
  if (explore_) {
    if (auto explored = exploreScriptWarp(via, value, cell)) {
      return explored;
    }
    if (choice_) {
      return std::nullopt;
    }
  }
  if (cell.warp != nullptr && cell.warp->targetMap) {
    const auto index = static_cast<std::size_t>(cell.warp - scriptWarps_.data());
    if (!cell.decision.allowed.empty() && !std::ranges::contains(allowedReported_, index)) {
      allowedReported_.push_back(index);
      std::string allowed;
      for (const std::string& reason : cell.decision.allowed) {
        allowed += (allowed.empty() ? "" : "; ") + reason;
      }
      core::logInfo("{}", core::formatMessage(messages::ScriptWarpAllowed, mapId_, value,
                                              cell.warp->source, allowed));
    }
    if (!cell.ambiguous.empty() &&
        !std::ranges::contains(ambiguousReported_, std::pair{mapId_, value})) {
      ambiguousReported_.emplace_back(mapId_, value);
      std::string places;
      for (const ScriptWarp* warp : cell.ambiguous) {
        places += std::format("{}{} exit {}", places.empty() ? "" : ", ",
                              warp->targetMap.value_or("?"), warp->targetExit);
      }
      core::logInfo("{}", core::formatMessage(messages::ScriptWarpAmbiguous, mapId_, value, places,
                                              cell.warp->source));
    }
    MapChange change{.map = *cell.warp->targetMap, .exit = cell.warp->targetExit, .trigger = value};
    if (!exitOpen(change)) {
      return std::nullopt;
    }
    writeTriggersOf(*cell.warp);
    return change;
  }
  if (cell.undecided && !std::ranges::contains(undecidedReported_, std::pair{mapId_, value})) {
    undecidedReported_.emplace_back(mapId_, value);
    core::logInfo("{}", core::formatMessage(messages::ScriptWarpUndecided, mapId_, value));
  }
  return std::nullopt;
}

std::optional<MapChange> Field::exploreScriptWarp(WarpVia via, std::int32_t value,
                                                  const CellWarp& cell) {
  std::vector<const ScriptWarp*> places =
      explorationWarps(scriptWarps_, mapId_, via, value, hasMap_);
  const ScriptWarp* vanilla = cell.warp != nullptr && cell.warp->targetMap ? cell.warp : nullptr;
  const auto samePlace = [](const ScriptWarp* left, const ScriptWarp* right) {
    return left != nullptr && right != nullptr && left->targetMap == right->targetMap &&
           left->targetExit == right->targetExit;
  };
  // Where the original goes and nowhere else: the original's way, with its logs.
  if (places.empty() || (places.size() == 1 && samePlace(places.front(), vanilla))) {
    return std::nullopt;
  }
  if (places.size() == 1) {
    return takeExplored(*places.front(), vanilla, value);
  }
  DestinationChoice choice{.map = mapId_, .cell = value, .places = {}, .vanilla = std::nullopt};
  std::vector<const ScriptWarp*> shown;
  for (const ScriptWarp* place : places) {
    if (!place->targetMap) {
      continue; // explorationWarps gives none without a target; a place needs one to be shown
    }
    choice.places.push_back(
        MapChange{.map = *place->targetMap, .exit = place->targetExit, .trigger = value});
    if (!choice.vanilla && samePlace(place, vanilla)) {
      choice.vanilla = shown.size();
    }
    shown.push_back(place);
  }
  if (shown.empty()) {
    return std::nullopt;
  }
  core::logInfo("{}",
                core::formatMessage(messages::ExplorationChoice, mapId_, value, shown.size()));
  choice_ = std::move(choice);
  choiceWarps_ = std::move(shown);
  choiceVanilla_ = vanilla;
  return std::nullopt;
}

std::optional<MapChange> Field::takeExplored(const ScriptWarp& warp, const ScriptWarp* vanilla,
                                             std::int32_t value) {
  if (!warp.targetMap) {
    return std::nullopt;
  }
  const bool vanillaPlace = vanilla != nullptr && vanilla->targetMap == warp.targetMap &&
                            vanilla->targetExit == warp.targetExit;
  if (!vanillaPlace) {
    core::logInfo("{}", core::formatMessage(messages::ExplorationOpened, mapId_, value,
                                            *warp.targetMap, warp.targetExit, warp.source));
  }
  MapChange change{.map = *warp.targetMap, .exit = warp.targetExit, .trigger = value};
  if (!exitOpen(change)) {
    return std::nullopt;
  }
  // The story's own place is taken by the warp the original takes, so its sets_flags are the
  // original's (the first warp of a place, which the chooser shows, may be another path to it).
  writeTriggersOf(vanillaPlace ? *vanilla : warp);
  return change;
}

void Field::setStoryCells(bool storyCells) {
  if (storyCells_ == storyCells) {
    return;
  }
  storyCells_ = storyCells;
  core::logDebug("[port:field] checkMapInteraction (tamer.c:660): map {} cells run by {}", mapId_,
                 storyCells ? "the story" : "field/script_warps");
}

void Field::setExplore(bool explore) {
  if (explore_ == explore) {
    return;
  }
  explore_ = explore;
  core::logInfo("{}",
                core::formatMessage(explore ? messages::ExplorationOn : messages::ExplorationOff));
  if (!explore) {
    choice_.reset();
    choiceWarps_.clear();
    choiceVanilla_ = nullptr;
  }
  refreshScriptWarps();
}

void Field::chooseDestination(std::optional<std::size_t> place) {
  if (!choice_ || (place && *place >= choiceWarps_.size())) {
    return;
  }
  const ScriptWarp* warp = place ? choiceWarps_[*place] : nullptr;
  const ScriptWarp* vanilla = choiceVanilla_;
  const std::int32_t value = choice_->cell;
  choice_.reset();
  choiceWarps_.clear();
  choiceVanilla_ = nullptr;
  if (warp == nullptr) {
    return;
  }
  if (auto change = takeExplored(*warp, vanilla, value)) {
    leave(std::move(*change));
  }
}

bool Field::exitOpen(const MapChange& change) {
  if (!exitFilter_ || exitFilter_(mapId_, change.map)) {
    return true;
  }
  core::logDebug("[port:field] exit filter: {} cell {} to {} exit {} stays closed", mapId_,
                 change.trigger, change.map, change.exit);
  closedExit_ = change;
  exitClosedNow_ = true;
  return false;
}

std::optional<std::int32_t> Field::shadeNow() const {
  // fade.c: FADE_PROGRESS holds the running fade in's shade, or the one the last fade ended on.
  if (fadeInSeconds_) {
    return fadeInShade(rules_.transition, std::min(fadeFrame(*fadeInSeconds_), fadeInFrames_),
                       fadeInFrames_);
  }
  return fadeProgress_;
}

void Field::tickFadeIn(float seconds) {
  if (!fadeInSeconds_) {
    return;
  }
  *fadeInSeconds_ += seconds;
  if (fadeFrame(*fadeInSeconds_) > fadeInFrames_) {
    endFadeIn();
  }
}

void Field::endFadeIn() {
  fadeProgress_ = fadeInShade(rules_.transition, fadeInFrames_, fadeInFrames_);
  fadeInSeconds_.reset();
}

void Field::leave(MapChange change) {
  leavingFrom_ = shadeNow();
  shadeHeld_ = false;
  leaving_ = std::move(change);
  leavingSeconds_ = 0.0F;
  leavingWalks_ = true;
  leavingShowsName_ = true;
  changeTarget_ = leaving_->map;
  moveTarget_.reset();
  path_.clear();
}

void Field::planPath(glm::vec3 target) {
  moveTarget_.reset();
  path_.clear();
  pathStallSeconds_ = 0.0F;
  const CollisionCell clicked = assets::collisionCellAt(target);
  const glm::vec3 position = world_->registry().get<Transform>(player_).position;
  // Paths do not cross warps other than the one clicked.
  const auto passable = [this](CollisionCell cell) {
    return !keepsOff(collision_.trigger(cell).value_or(0));
  };
  // The clicked cell, or the nearest ring of cells around it with one the player can get to.
  for (int ring = 0; ring <= ClickSearchCells; ++ring) {
    std::vector<CollisionCell> goals;
    for (int dz = -ring; dz <= ring; ++dz) {
      for (int dx = -ring; dx <= ring; ++dx) {
        if (std::max(std::abs(dx), std::abs(dz)) == ring) {
          goals.push_back({.column = clicked.column + dx, .row = clicked.row + dz});
        }
      }
    }
    path_ = findPath(collision_, position, goals, rules_.player.radius, passable);
    if (path_.empty()) {
      continue;
    }
    // In the clicked cell, the last step goes from its centre to the clicked point if nothing is in
    // the way.
    const glm::vec3 point{target.x, path_.back().y, target.z};
    const glm::vec3 centre = path_.back();
    const GridMove last =
        moveOnGrid(collision_, centre, ground(point) - ground(centre), rules_.player.radius);
    if (ring == 0 && !last.blockedX && !last.blockedZ) {
      path_.push_back(point);
    }
    moveTarget_ = path_.back();
    return;
  }
}

// tamer.c: tamerTickWalkingState: the direction pressed, relative to the camera, and run unless
// walk is held (walk is animation 2, run animation 3).
Field::Step Field::playerStep() {
  const float length = glm::length(input_.stick);
  if (length > rules_.input.deadZone) {
    moveTarget_.reset();
    path_.clear();
    const glm::vec3 right{-cameraForward_.z, 0.0F, cameraForward_.x};
    const glm::vec3 direction = (right * input_.stick.x) + (cameraForward_ * input_.stick.y);
    return {.direction = glm::normalize(ground(direction)), .run = input_.run};
  }
  if (!moveTarget_) {
    return {};
  }
  return {.direction = glm::vec2{0.0F}, .run = input_.run, .followPath = true};
}

bool Field::playerWantsToMove() const {
  return glm::length(input_.stick) > rules_.input.deadZone || moveTarget_.has_value();
}

std::int32_t Field::takeFrames(float seconds) {
  // A little slack, so that ticks adding up to a frame's length start it despite rounding.
  constexpr float Slack = 1e-4F;
  // A long tick (a debugger stop) runs this many frames at most and drops the rest.
  constexpr std::int32_t MaxFrames = 8;
  const float frame = rules_.transition.frameSeconds;
  // The engine's: the original reads the pad on its next frame, up to two of the console's
  // retraces later; a press from standing starts a frame at once.
  const bool wants = playerWantsToMove();
  if (wants && !playerPressed_ && playerMotion_ == Motion::Idle) {
    frameClock_ = std::max(frameClock_, frame);
  }
  playerPressed_ = wants;
  std::int32_t frames = 0;
  while (frameClock_ + Slack >= frame && frames < MaxFrames) {
    frameClock_ = std::max(0.0F, frameClock_ - frame);
    ++frames;
  }
  if (frames == MaxFrames) {
    frameClock_ = 0.0F;
  }
  frameClock_ += seconds;
  return frames;
}

void Field::movePlayer(std::int32_t frames) {
  if (frames > 0) {
    startGlide(playerGlide_, player_);
    for (std::int32_t frame = 0; frame < frames; ++frame) {
      stepPlayer(playerStep());
    }
    endGlide(playerGlide_, player_);
  }
  // The original faces the way the pad points even against a wall (tamer.c: setTamerDirection),
  // and keeps its walk or run animation playing at full speed there: it is the animation that moves
  // it (anim.c: applyRootMomentum), and the collision only cancels the movement.
  world_->registry().get<AnimationIntent>(player_) = {.motion = playerMotion_, .speedScale = 1.0F};
}

void Field::stepPlayer(const Step& step) {
  auto& transform = world_->registry().get<Transform>(player_);
  const MoverRules& mover = rules_.player;
  const float frame = rules_.transition.frameSeconds;
  // tamer.c: tamerTickWalkingState: with the pad pressed, run (animation 3), or walk (2) with walk
  // held; without it, stand (0).
  Motion wanted = Motion::Idle;
  if (step.direction != glm::vec2{0.0F} || step.followPath) {
    wanted = step.run ? Motion::Run : Motion::Walk;
  }
  // anim.c: startAnimation zeroes the root's momentum: the frame a clip starts, the player turns
  // but does not move.
  const bool starts = wanted != playerMotion_;
  playerMotion_ = wanted;
  if (wanted == Motion::Idle) {
    return;
  }
  const float distance = (wanted == Motion::Run ? mover.runSpeed : mover.walkSpeed) * frame;
  if (!step.followPath) {
    transform.yaw = turnToward(transform.yaw,
                               assets::yawOfDirection({step.direction.x, 0.0F, step.direction.y}),
                               mover.turnSpeed * frame);
    if (!starts) {
      walkForward(distance);
    }
    return;
  }
  if (starts) {
    return;
  }
  // Click-to-move, the engine's: the path's own steps, sliding along walls.
  const glm::vec3 start = transform.position;
  const glm::vec2 facing = followPath(transform.position, distance);
  if (facing == glm::vec2{0.0F}) {
    playerMotion_ = Motion::Idle;
    return;
  }
  // tamer.c: checkMapInteraction stops the player at an NPC (animFlag 2).
  if (npcs_.blocks(start, transform.position, mover.radius)) {
    transform.position = start;
  }
  transform.yaw = turnToward(transform.yaw, assets::yawOfDirection({facing.x, 0.0F, facing.y}),
                             mover.turnSpeed * frame);
  if (!moveTarget_) {
    return;
  }
  const float moved = groundDistance(transform.position, start);
  pathStallSeconds_ = moved < distance * StuckFraction ? pathStallSeconds_ + frame : 0.0F;
  if (pathStallSeconds_ > PathStallSeconds) {
    pathStallSeconds_ = 0.0F;
    if (!path_.empty()) {
      path_.erase(path_.begin());
    }
    if (path_.empty()) {
      moveTarget_.reset();
    }
  }
}

bool Field::playerTouchesSomeone(glm::vec3 from, glm::vec2 step) const {
  const float radius = rules_.player.radius;
  const glm::vec3 to = from + glm::vec3{step.x, 0.0F, step.y};
  if (npcs_.blocks(from, to, radius)) {
    return true;
  }
  // map.c: entityCheckEntityCollision: the player, in the field, touches the partner's square on
  // any side.
  const GroundSquare self{.centre = ground(to), .half = radius};
  const GroundSquare partner{.centre = ground(world_->registry().get<Transform>(partner_).position),
                             .half = rules_.partner.mover.radius};
  return squaresOverlap(self, partner);
}

void Field::walkForward(float distance) {
  auto& transform = world_->registry().get<Transform>(player_);
  const float radius = rules_.player.radius;
  const glm::vec3 start = transform.position;
  const glm::vec2 step = still(ground(assets::forwardOfYaw(transform.yaw)) * distance);
  // tamer.c: checkMapInteraction tests the whole step ahead (entityCheckCollision): the map first,
  // then the NPCs, past the partner (it is the test's source). An NPC stops the player (animFlag
  // 2); the map turns it aside (collisionGrace).
  if (!stepCollides(collision_, start, step, radius)) {
    // anim.c: applyRootMomentum: the whole step, the map tested no more.
    if (!npcs_.blocks(start, start + glm::vec3{step.x, 0.0F, step.y}, radius)) {
      transform.position += glm::vec3{step.x, 0.0F, step.y};
    }
    return;
  }
  // The grace counts the partner among what blocks a side. tamerTickWalkingState sets the pad's
  // rotation again next frame, so the turn does not add up.
  const Grace grace = collisionGrace(transform.yaw, distance, [&](glm::vec2 probe) {
    return stepCollides(collision_, start, probe, radius) || playerTouchesSomeone(start, probe);
  });
  transform.yaw = grace.yaw;
  // anim.c: applyRootMomentum moves by the step along the turned facing less the blocked sides'
  // parts and tests the map no more: a step aside of a wall's edge goes on until every side is
  // blocked (measured in trop01 at (2788, -1494): right turns to 0xA50 and goes (+78, +61) in 30
  // frames, not along the wall).
  transform.position += glm::vec3{grace.step.x, 0.0F, grace.step.y};
}

glm::vec2 Field::followPath(glm::vec3& position, float distance) {
  // Walks from waypoint to waypoint, carrying what is left of the tick's distance from one to the
  // next, so the player stays on the lines the path was planned on.
  glm::vec2 facing{0.0F};
  float left = distance;
  while (left > 0.0F && !path_.empty()) {
    const glm::vec3 aim = path_.front();
    const float away = groundDistance(aim, position);
    if (away <= WaypointReach) {
      path_.erase(path_.begin());
      continue;
    }
    facing = (ground(aim) - ground(position)) / away;
    const float length = std::min(left, away);
    const GridMove move = moveOnGrid(collision_, position, facing * length, rules_.player.radius);
    const bool blocked = move.blockedX || move.blockedZ;
    left -= blocked ? groundDistance(move.position, position) : length;
    position = move.position;
    if (!blocked) {
      if (length == away) {
        position.x = aim.x;
        position.z = aim.z;
        path_.erase(path_.begin());
      }
      continue;
    }
    if (groundDistance(aim, position) <= WaypointReach) {
      path_.erase(path_.begin());
      continue;
    }
    // A wall: sliding is all this tick can do. Pressed against it close to the end is there.
    if (path_.size() == 1 && groundDistance(aim, position) <= rules_.input.arriveDistance) {
      path_.clear();
    }
    break;
  }
  if (path_.empty()) {
    moveTarget_.reset();
  }
  return facing;
}

// tamer.c: checkMapInteraction and map_collision.c: getTileTrigger: the exits band (110-119 in the
// original, field/triggers) warps through exit value - 110; the other values go to later systems.
// The original tests the cell every frame; this fires on stepping onto it. The cell is the one
// getTileTrigger reads (originalCellAt): on a cell's edge it is the neighbour away from 0, as where
// frzl17's exit 1 puts the player (-400, 1400), on the curling's start cell.
void Field::checkTriggers() {
  exitClosedNow_ = false;
  const glm::vec3 position = world_->registry().get<Transform>(player_).position;
  const CollisionCell cell = assets::originalCellAt(position);
  const std::int32_t value = collision_.trigger(cell).value_or(0);
  if (value != checkedCell_) {
    checkedCell_ = value;
    core::logDebug("[port:field] checkMapInteraction (tamer.c:660): map {} cell {}", mapId_, value);
  }
  // tamer.c: checkMapInteraction asks for a step cell's section every frame on it.
  if (warpViaOf(value, rules_.triggers) == WarpVia::Step &&
      !(staticWarps() && std::ranges::contains(warpingStepCells_, value))) {
    cellSections_.push_back(value);
  }
  if (value == lastTrigger_) {
    return;
  }
  lastTrigger_ = value;
  if (value == 0) {
    return;
  }
  if (const auto exit = rules_.triggers.exitOf(value)) {
    const auto warp = std::ranges::find(warps_, *exit, &assets::MapWarp::exit);
    const std::optional<std::string> target =
        warp != warps_.end() ? warp->targetMapId : std::nullopt;
    if (target) {
      MapChange change{.map = *target, .exit = warp->targetExit, .trigger = value};
      if (exitOpen(change)) {
        leave(std::move(change));
      }
      return;
    }
  }
  if (staticWarps() && warpViaOf(value, rules_.triggers) == WarpVia::Step) {
    if (auto scripted = takeScriptWarp(WarpVia::Step, value)) {
      leave(std::move(*scripted));
      return;
    }
    if (choice_ || exitClosedNow_) {
      return;
    }
  }
  triggerSteps_.push_back({.trigger = value, .cell = cell});
}

// tamer.c: checkMapInteraction runs the section of a cell of the confirm band (80-109 in the
// original, field/triggers) the player stands on when
// confirm is pressed, only while TAMER_STATE is 0 (tamer.c:685; tamerTickOverworld runs
// tamerTickWalkingState, tamer.c:455) and no script runs (IS_SCRIPT_PAUSED 1: the story drops the
// section). Here: while the tick gets this far (no fade in, not leaving). That is the same only
// because the field has none of the other states yet (6 idle, 7 item pickup, 8 training, 11
// fishing, 14 chest, 15-20 opening, ending and life loss). Before the cell, the original lets
// confirm talk to an NPC the player faces: then the cell's section loses (touchNpcs).
void Field::checkConfirm() {
  exitClosedNow_ = false;
  if (!std::exchange(input_.confirm, false) || leaving_) {
    return;
  }
  const auto& transform = world_->registry().get<Transform>(player_);
  std::int32_t value = collision_.trigger(assets::originalCellAt(transform.position)).value_or(0);
  if (generousTalk_ && warpViaOf(value, rules_.triggers) != WarpVia::Confirm) {
    // The generous talk range: the sign's cell in front of the player.
    const glm::vec3 ahead =
        transform.position + (assets::forwardOfYaw(transform.yaw) * GenerousSignAhead);
    value = collision_.trigger(assets::originalCellAt(ahead)).value_or(0);
  }
  if (warpViaOf(value, rules_.triggers) != WarpVia::Confirm) {
    return;
  }
  if (staticWarps()) {
    if (auto scripted = takeScriptWarp(WarpVia::Confirm, value)) {
      leave(std::move(*scripted));
      return;
    }
    if (exitClosedNow_) {
      return;
    }
  }
  cellSections_.push_back(value);
}

void Field::holdForCare(bool held, CareClock clock) {
  if (held) {
    careClock_ = clock;
  }
  if (held == careHeld_) {
    return;
  }
  careHeld_ = held;
  careWalk_.reset();
  if (!held && !scriptHeld_) {
    // partner.c: setCameraFollowPlayer.
    scriptCamera_.reset();
    cameraPan_.reset();
  }
  moveTarget_.reset();
  path_.clear();
  partnerStuckSeconds_ = 0.0F;
  entt::registry& registry = world_->registry();
  const Motion partnerMotion = registry.get<AnimationIntent>(partner_).motion;
  registry.get<AnimationIntent>(player_) = {};
  registry.get<AnimationIntent>(partner_) = {};
  core::logDebug("[port:care] tamerSetState({}) (partner.c: the care scenes hold the player)",
                 held ? "6" : "0");
  if (!held) {
    releaseHandoff("care");
    // partner.c: tickPartnerToilet sub state 4 lets the script go with the walk clip still on, and
    // partnerTickWalking (state 1) goes on from it: a partner let go walking walks on, it does not
    // stand first.
    if (!scriptHeld_ && (partnerMotion == Motion::Walk || partnerMotion == Motion::Run)) {
      // On the clip's frame the walk got to: its pass goes on before the follow picks another.
      const auto frame =
          static_cast<std::size_t>(std::lround(careClipSeconds_ / rules_.transition.frameSeconds));
      playPartnerFrom(partnerMotion == Motion::Run ? PartnerRunClip : PartnerWalkClip, frame, {});
    }
  }
}

void Field::walkPartnerToRange(float range) {
  if (!careHeld_) {
    return;
  }
  entt::registry& registry = world_->registry();
  const float distance = groundDistance(registry.get<Transform>(partner_).position,
                                        registry.get<Transform>(player_).position);
  // partner.c: checkEatDistance is close at the range or nearer; close walks away (sub state 1),
  // far walks up (sub state 2).
  careWalk_ = CareWalk{
      .range = range, .away = distance <= range, .point = std::nullopt, .motion = Motion::Walk};
  partnerStuckSeconds_ = 0.0F;
  core::logDebug("[port:care] partnerTickFeedItem (partner.c:434): distance {:.0f}, range {:.0f}: "
                 "sub state {}",
                 distance, range, careWalk_->away ? 1 : 2);
}

void Field::tickCare(float seconds) {
  // clock.c: tickGameClock runs: the partner states that hold the player keep the game time, but
  // the sleep's (partner.c: partnerTickSleep, stopGameTime).
  gameTimeRan_ = careClock_ == CareClock::Runs;
  entt::registry& registry = world_->registry();
  // tamer.c: tamerTickIdle: the partner's care holds the player (its state 6), who stands with
  // animation 0 until let go.
  registry.get<AnimationIntent>(player_) = {};
  tickCareWalk(seconds);
  tickCameraPan(seconds);
  tickNpcs(seconds, false);
}

void Field::holdForItem(ItemHold hold) {
  if (hold == itemHold_) {
    return;
  }
  std::int32_t state = 0;
  if (hold == ItemHold::Ground) {
    state = 7;
  } else if (hold == ItemHold::Chest) {
    state = 14;
  }
  core::logDebug("[port:field_items] tamerSetState({})", state);
  itemHold_ = hold;
  moveTarget_.reset();
  path_.clear();
  partnerStuckSeconds_ = 0.0F;
  entt::registry& registry = world_->registry();
  registry.get<AnimationIntent>(player_) = {};
  if (hold == ItemHold::Chest) {
    registry.get<AnimationIntent>(partner_) = {};
    follow_.standHeld();
  }
}

void Field::turnPlayerTo(glm::vec3 point) {
  auto& player = world_->registry().get<Transform>(player_);
  const glm::vec3 toPoint = point - player.position;
  if (toPoint.x != 0.0F || toPoint.z != 0.0F) {
    // At once (entityLookAtLocation): nothing to draw between the old facing and the new.
    player.yaw = assets::yawOfDirection(toPoint);
    player.previousYaw = player.yaw;
  }
}

void Field::tickItemHold(float seconds, bool wasFollowing) {
  // clock.c: tickGameClock runs on: the notice holds the player's state only.
  gameTimeRan_ = true;
  // The notice takes the presses: none walks the player or reaches a cell or an NPC.
  input_ = {};
  entt::registry& registry = world_->registry();
  playerMotion_ = Motion::Idle;
  registry.get<AnimationIntent>(player_) = {};
  if (itemHold_ == ItemHold::Ground) {
    // partner.c: partnerTickNormal goes on while tamerTickPickupItem holds the player: the partner
    // follows and turns to the player as it does while the player stands.
    const std::int32_t frames = takeFrames(seconds);
    if (frames == 0) {
      partnerFollowing_ = wasFollowing;
    }
    movePartnerFrames(frames);
  }
  // overworld.c: NPCEntityTickOverworld walks the NPCs on; their talks and touches wait for the
  // player's state 0.
  tickNpcs(seconds, false);
}

void Field::faceEachOther() {
  entt::registry& registry = world_->registry();
  auto& partner = registry.get<Transform>(partner_);
  auto& player = registry.get<Transform>(player_);
  const glm::vec3 toPlayer = player.position - partner.position;
  if (toPlayer.x != 0.0F || toPlayer.z != 0.0F) {
    // At once (partner.c: entityLookAtLocation): nothing to draw between the old facings.
    partner.yaw = assets::yawOfDirection(toPlayer);
    player.yaw = assets::yawOfDirection(-toPlayer);
    partner.previousYaw = partner.yaw;
    player.previousYaw = player.yaw;
  }
}

void Field::walkPartnerTo(glm::vec3 point, Motion motion) {
  if (!careHeld_) {
    return;
  }
  careWalk_ = CareWalk{.range = 0.0F, .away = false, .point = point, .motion = motion};
  partnerStuckSeconds_ = 0.0F;
  // tickEntityWalkTo's caller starts the clip as it sets the walk (startAnimation): the partner
  // walks from this frame, not after a frame of standing. A walk on with the clip a walk just
  // ended on goes on with it (tickPartnerToilet sub states 1 and 2 keep sub state 0's run).
  auto& intent = world_->registry().get<AnimationIntent>(partner_);
  if (intent.motion != motion) {
    careClipSeconds_ = 0.0F;
  }
  intent = {.motion = motion, .speedScale = 1.0F};
}

void Field::runPartnerUp() {
  if (!careHeld_) {
    return;
  }
  const PartnerRules& rules = rules_.partner;
  careWalk_ = CareWalk{.range = rules.runDistance * rules.mover.radius,
                       .away = false,
                       .point = std::nullopt,
                       .motion = Motion::Run};
  partnerStuckSeconds_ = 0.0F;
}

void Field::tickCareWalk(float seconds) {
  if (!careWalk_) {
    return;
  }
  entt::registry& registry = world_->registry();
  const auto& transform = registry.get<Transform>(partner_);
  const glm::vec3 playerPosition = registry.get<Transform>(player_).position;
  const bool toPoint = careWalk_->point.has_value();
  const glm::vec3 target = careWalk_->point.value_or(playerPosition);
  const float distance = groundDistance(transform.position, target);
  bool there = false;
  if (toPoint) {
    // tickEntityWalkTo: there once on the point's cell (getModelTile).
    there = distance <= CareArrival ||
            assets::originalCellAt(transform.position) == assets::originalCellAt(target);
  } else {
    there = careWalk_->away ? distance > careWalk_->range : distance <= careWalk_->range;
  }
  // The original walks on into a wall; the engine gives up once stuck, where it stands.
  if (there || partnerStuckSeconds_ > rules_.partner.stuckSeconds) {
    if (!there) {
      core::logDebug("[port:care] the partner is stuck {:.0f} short of its care walk's end: it "
                     "stops there",
                     toPoint ? distance : std::abs(distance - careWalk_->range));
    }
    // tickEntityWalkTo ends on the point with the walk clip still on (tickPartnerToilet sub
    // states 1, 2 and 4): the next step starts its own clip.
    if (toPoint && there) {
      registry.get<AnimationIntent>(partner_) = {.motion = careWalk_->motion, .speedScale = 1.0F};
    }
    careWalk_.reset();
    partnerStuckSeconds_ = 0.0F;
    if (!toPoint) {
      faceEachOther();
    }
    return;
  }
  const glm::vec2 toTarget = ground(target) - ground(transform.position);
  glm::vec2 heading = ground(assets::forwardOfYaw(transform.yaw));
  if (glm::length(toTarget) > 0.0F) {
    heading = glm::normalize(toTarget) * (careWalk_->away ? -1.0F : 1.0F);
  }
  // A walk to a point stops on it (tickEntityWalkTo); one to a range walks on until it crosses it.
  stepCareWalk(heading, toPoint ? distance : std::numeric_limits<float>::max(), careWalk_->motion,
               seconds, !toPoint);
  careClipSeconds_ += seconds;
}

void Field::stepCareWalk(glm::vec2 heading, float distance, Motion motion, float seconds,
                         bool collides) {
  entt::registry& registry = world_->registry();
  auto& transform = registry.get<Transform>(partner_);
  const PartnerRules& rules = rules_.partner;
  const float expected =
      (motion == Motion::Run ? rules.mover.runSpeed : rules.mover.walkSpeed) * seconds;
  const glm::vec2 step = heading * std::min(expected, distance);
  glm::vec3 reached = transform.position + glm::vec3{step.x, 0.0F, step.y};
  if (collides) {
    const GridMove move = moveOnGrid(collision_, transform.position, step, rules.mover.radius);
    reached = npcs_.blocks(transform.position, move.position, rules.mover.radius)
                  ? transform.position
                  : move.position;
  }
  const float moved = groundDistance(reached, transform.position);
  transform.position = reached;
  transform.yaw = assets::yawOfDirection({heading.x, 0.0F, heading.y});
  registry.get<AnimationIntent>(partner_) = {
      .motion = motion,
      .speedScale = expected > 0.0F ? std::clamp(moved / expected, 0.0F, 1.0F) : 1.0F};
  const float wanted = std::min(expected, distance);
  partnerStuckSeconds_ = moved < wanted * StuckFraction ? partnerStuckSeconds_ + seconds : 0.0F;
  // partner.c: entityLookAtLocation: the player looks at the partner meanwhile.
  auto& player = registry.get<Transform>(player_);
  const glm::vec3 toPartner = transform.position - player.position;
  if (toPartner.x != 0.0F || toPartner.z != 0.0F) {
    player.yaw = assets::yawOfDirection(toPartner);
  }
}

void Field::movePartnerFrames(std::int32_t frames) {
  auto& animation = world_->registry().get<AnimationIntent>(partner_);
  if (frames == 0) {
    animation = partnerIntent_;
    return;
  }
  startGlide(partnerGlide_, partner_);
  for (std::int32_t frame = 0; frame < frames; ++frame) {
    movePartner(rules_.transition.frameSeconds);
  }
  endGlide(partnerGlide_, partner_);
  partnerIntent_ = animation;
}

void Field::movePartner(float seconds) {
  entt::registry& registry = world_->registry();
  const glm::vec3 playerPosition = registry.get<Transform>(player_).position;
  const PartnerRules& rules = rules_.partner;
  partnerFollowing_ = false;
  // The engine's rescue of a partner left far behind (a sulking partner walks while the player
  // runs, partner.c: partnerTickWalking): it comes back from just off the screen's edge, so it is
  // never seen to jump (the original brings it there from off screen, map.c:
  // tickPartnerWaypoints).
  const glm::vec3 partnerPosition = registry.get<Transform>(partner_).position;
  if (groundDistance(partnerPosition, playerPosition) > rules.teleportDistance) {
    if (!offScreen_) {
      placePartnerNearPlayer();
      return;
    }
    // One still in view walks on, as in the original.
    if (offScreen_(partnerPosition) && placePartnerOffScreen()) {
      return;
    }
  }
  if (partnerMakesRoom_ && makeRoomForPlayer(seconds)) {
    return;
  }
  syncPartnerFollow();
  const OriginalPoint before = follow_.position();
  const float height = registry.get<Transform>(partner_).position.y;
  const float radius = rules.mover.radius;
  follow_.tick({.map = &collision_,
                .player = originalPointOf(playerPosition),
                .playerRadius = static_cast<std::int32_t>(std::lround(rules_.player.radius)),
                .npcBlocks =
                    [&](OriginalPoint from, OriginalPoint to) {
                      return npcs_.blocks(worldPointOf(from, height), worldPointOf(to, height),
                                          radius);
                    },
                .mood = partnerMood_,
                .randomBelow = partnerRandom_});
  showPartnerFollow();
  partnerFollowing_ = true;
  const Motion motion = motionOfClip(follow_.clip().id);
  registry.get<AnimationIntent>(partner_) = {.motion = motion, .speedScale = 1.0F};
  // A walk that goes nowhere for a while (against a wall or turning aside on the spot) is taken
  // next to the player.
  const bool stuck = motion != Motion::Idle && follow_.position() == before;
  partnerStuckSeconds_ = stuck ? partnerStuckSeconds_ + seconds : 0.0F;
  if (partnerStuckSeconds_ > rules.stuckSeconds) {
    placePartnerNearPlayer();
  }
}

void Field::syncPartnerFollow() {
  const auto& transform = world_->registry().get<Transform>(partner_);
  if (followPose_ && followPose_->position == transform.position &&
      followPose_->yaw == transform.yaw) {
    return;
  }
  follow_.place(originalPointOf(transform.position), assets::yawToOriginal(transform.yaw));
  followPose_ = scene::Pose{.position = transform.position, .yaw = transform.yaw};
}

void Field::showPartnerFollow() {
  auto& transform = world_->registry().get<Transform>(partner_);
  transform.position = worldPointOf(follow_.position(), transform.position.y);
  transform.yaw = std::remainder(assets::yawFromOriginal(follow_.rotation()), Turn);
  followPose_ = scene::Pose{.position = transform.position, .yaw = transform.yaw};
}

bool Field::makeRoomForPlayer(float seconds) {
  // The engine's: the original's partner stays where a script or talk left it until the player
  // walks out of its stopping distance (partner.c:971, its closeness), against the player,
  // inside the NPC it talked to, or between the camera and the player, hiding it. Here, while the
  // player stands, it first walks off to halfway between touching the player and its stopping
  // distance, and out of the camera's line to the player, then follows as the original.
  entt::registry& registry = world_->registry();
  auto& transform = registry.get<Transform>(partner_);
  const glm::vec3 playerPosition = registry.get<Transform>(player_).position;
  const PartnerRules& rules = rules_.partner;
  const float radius = rules.mover.radius;
  const float touching = rules_.player.radius + radius;
  const float stops = rules.walkDistance * radius;
  const float roomy = (touching + stops) / 2.0F;
  const glm::vec2 away = ground(transform.position) - ground(playerPosition);
  const float distance = glm::length(away);
  const glm::vec2 side{-cameraView_.y, cameraView_.x};
  const float across = glm::dot(away, side);
  const bool hides =
      glm::dot(away, cameraView_) < 0.0F && std::abs(across) < touching * HiddenFraction;
  const bool tight = distance < roomy || npcs_.overlaps(transform.position, radius);
  // Once the player walks off, the partner follows it as the original.
  const bool playerMoves = registry.get<AnimationIntent>(player_).motion != Motion::Idle;
  if ((!tight && !hides) || distance >= stops || playerMoves) {
    partnerMakesRoom_ = false;
    return false;
  }
  const glm::vec2 straight =
      distance > 0.0F ? away / distance : -ground(assets::forwardOfYaw(transform.yaw));
  const float expected = rules.mover.walkSpeed * seconds;
  const float step = hides || distance >= roomy ? expected : std::min(expected, roomy - distance);
  std::vector<glm::vec2> headings;
  if (hides) {
    headings.push_back(across < 0.0F ? -side : side);
  }
  for (const float turn : RoomTurns) {
    const float angle = turn * Turn;
    headings.emplace_back((straight.x * std::cos(angle)) - (straight.y * std::sin(angle)),
                          (straight.x * std::sin(angle)) + (straight.y * std::cos(angle)));
  }
  for (const glm::vec2 heading : headings) {
    const GridMove move = moveOnGrid(collision_, transform.position, heading * step, radius);
    if (npcs_.blocks(transform.position, move.position, radius) ||
        groundDistance(move.position, transform.position) < step * StuckFraction ||
        groundDistance(move.position, playerPosition) <= distance) {
      continue;
    }
    transform.position = move.position;
    transform.yaw = assets::yawOfDirection({heading.x, 0.0F, heading.y});
    registry.get<AnimationIntent>(partner_) = {.motion = Motion::Walk, .speedScale = 1.0F};
    partnerStuckSeconds_ = 0.0F;
    return true;
  }
  // Walled in: it stays, as the original does.
  core::logDebug("[port:partner] no room to step away from the player: the partner stays {:.0f} "
                 "from it",
                 distance);
  partnerMakesRoom_ = false;
  return false;
}

bool Field::placePartnerAt(const assets::SpawnPoint& spot) {
  const float radius = rules_.partner.mover.radius;
  // entityCheckCollision there looks for an NPC under the partner; the player may stand close.
  const GroundSquare there{.centre = ground(spot.position), .half = radius};
  const std::vector<Body> others = bodies();
  const bool clear = std::ranges::none_of(
      others, [&](const Body& body) { return body.who >= 2 && overlaps(there, squareOf(body)); });
  // The original puts it back without looking at the grid; a spot inside a wall (a save edited by
  // hand or from another map) is the only one refused.
  if (!clear || collision_.isBlocked(assets::collisionCellAt(spot.position))) {
    core::logDebug(
        "[port:field] initializeLoadedMap (main.c:3803): the partner's saved spot ({:.0f}, "
        "{:.0f}) is taken, it stands next to the player",
        spot.position.x, spot.position.z);
    return false;
  }
  entt::registry& registry = world_->registry();
  scene::place(registry.get<Transform>(partner_), spot.position, spot.yaw);
  registry.get<AnimationIntent>(partner_) = {};
  follow_.forgetTrail(originalPointOf(registry.get<Transform>(player_).position));
  partnerStuckSeconds_ = 0.0F;
  partnerFollowing_ = false;
  core::logDebug("[port:field] initializeLoadedMap (main.c:3803): the partner back at its saved "
                 "spot ({:.0f}, {:.0f})",
                 spot.position.x, spot.position.z);
  return true;
}

bool Field::placePartnerOffScreen() {
  // map.c: getClosestTileOffScreen: a line of cells from the player's toward the partner's, one
  // step along the longer axis and the shorter one's share carried in an accumulator; the first
  // cell off screen. The engine also wants it free, in a clear line from the player and a cell
  // nearer than the partner already is.
  entt::registry& registry = world_->registry();
  const glm::vec3 player = registry.get<Transform>(player_).position;
  const glm::vec3 partner = registry.get<Transform>(partner_).position;
  const float nearer = groundDistance(partner, player) - assets::CollisionCellSize;
  const assets::CollisionCell from = assets::originalCellAt(player);
  const assets::CollisionCell to = assets::originalCellAt(partner);
  const std::int32_t dx = to.column - from.column;
  const std::int32_t dy = to.row - from.row;
  const bool alongX = std::abs(dx) >= std::abs(dy);
  const std::int32_t length = alongX ? std::abs(dx) : std::abs(dy);
  if (length == 0) {
    return false;
  }
  const std::int32_t minor = alongX ? dy : dx;
  const std::int32_t share = minor / length;
  const std::int32_t sign = minor % length < 0 ? -1 : 1;
  const std::int32_t rest = std::abs(minor % length);
  const std::int32_t majorStep = (alongX ? dx : dy) > 0 ? 1 : -1;
  std::int32_t major = alongX ? from.column : from.row;
  std::int32_t across = alongX ? from.row : from.column;
  std::int32_t accumulator = length;
  const float radius = rules_.partner.mover.radius;
  for (std::int32_t step = 0; step < length; ++step) {
    if (rest != 0) {
      accumulator -= rest;
      if (accumulator <= 0) {
        across += sign;
        accumulator += length;
      }
    } else {
      across += share;
    }
    major += majorStep;
    const assets::CollisionCell cell = alongX
                                           ? assets::CollisionCell{.column = major, .row = across}
                                           : assets::CollisionCell{.column = across, .row = major};
    const glm::vec3 centre = assets::collisionCellCenter(cell);
    if (!offScreen_(centre)) {
      continue;
    }
    if (groundDistance(centre, player) > nearer || !fitsOnGrid(collision_, centre, radius) ||
        !isLineClear(collision_, player, centre, 0.0F)) {
      return false;
    }
    glm::vec3 chosen = centre;
    chosen.y = player.y;
    scene::place(registry.get<Transform>(partner_), chosen,
                 assets::yawOfDirection(player - chosen));
    registry.get<AnimationIntent>(partner_) = {};
    follow_.forgetTrail(originalPointOf(player));
    partnerStuckSeconds_ = 0.0F;
    partnerFollowing_ = false;
    ++partnerTeleports_;
    core::logDebug("[port:field] getClosestTileOffScreen (map.c): the partner, too far, comes back "
                   "from cell ({}, {}), {:.0f} from the player",
                   cell.column, cell.row, groundDistance(chosen, player));
    return true;
  }
  return false;
}

void Field::placePartnerNearPlayer() {
  entt::registry& registry = world_->registry();
  const auto& player = registry.get<Transform>(player_);
  const float radius = rules_.partner.mover.radius;
  std::vector<glm::vec3> candidates{
      player.position - (assets::forwardOfYaw(player.yaw) * rules_.partner.spawnDistance)};
  for (const CollisionCell cell : std::views::reverse(follow_.trail())) {
    candidates.push_back(assets::collisionCellCenter(cell));
  }
  candidates.push_back(assets::collisionCellCenter(lastPlayerCell_));
  constexpr std::array<glm::vec2, 8> Around = {
      {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
  for (const glm::vec2 offset : Around) {
    candidates.push_back(player.position +
                         (glm::vec3{offset.x, 0.0F, offset.y} * rules_.partner.spawnDistance));
  }
  glm::vec3 chosen = player.position;
  const std::vector<Body> others = bodies();
  const auto clearOfOthers = [&](glm::vec3 candidate) {
    const GroundSquare there{.centre = ground(candidate), .half = radius};
    return std::ranges::none_of(
        others, [&](const Body& body) { return body.who != 1 && overlaps(there, squareOf(body)); });
  };
  for (const glm::vec3 candidate : candidates) {
    if (fitsOnGrid(collision_, candidate, radius) &&
        isLineClear(collision_, player.position, candidate, 0.0F) && clearOfOthers(candidate)) {
      chosen = candidate;
      break;
    }
  }
  chosen.y = player.position.y;
  scene::place(registry.get<Transform>(partner_), chosen,
               assets::yawOfDirection(player.position - chosen));
  registry.get<AnimationIntent>(partner_) = {};
  follow_.forgetTrail(originalPointOf(player.position));
  partnerStuckSeconds_ = 0.0F;
  partnerFollowing_ = false;
  ++partnerTeleports_;
}

} // namespace dwe::game::field
