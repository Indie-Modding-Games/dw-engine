#include <dwe/core/log.hpp>
#include <dwe/game/field/grid.hpp>
#include <dwe/game/field/npcs.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

namespace dwe::game::field {

namespace {

using scene::AnimationIntent;
using scene::Motion;
using scene::Transform;

constexpr float Turn = 2.0F * std::numbers::pi_v<float>;
// A move shorter than this along an axis does not move along it (map.c: entityCheckCollision zeroes
// the axis a cardinal facing does not move on).
constexpr float AxisEpsilon = 1e-4F;

// MapDigimonPath.aiSections: what each part of an NPC's path does (overworld.c:
// NPCEntityTickWaypointAI).
constexpr std::int32_t PathWait = 0;
constexpr std::int32_t PathWalk = 1;
constexpr std::int32_t PathRun = 2;
constexpr std::int32_t PathEnd = -1;
// NPCEntityIsInTrackingRadius: trackingRange * radiusTenths / 10.
constexpr std::int32_t Tenths = 10;
// lookAtTamerState: the steps of a reaction.
constexpr std::uint8_t TrackUnseen = 0;
constexpr std::uint8_t TrackStep1 = 1;
constexpr std::uint8_t TrackStep2 = 2;
constexpr std::uint8_t TrackStep3 = 3;
constexpr std::uint8_t TrackStep4 = 4;

glm::vec2 ground(glm::vec3 v) {
  return {v.x, v.z};
}

bool spansOverlap(float aMin, float aMax, float bMin, float bMax) {
  return aMax >= bMin && aMin <= bMax;
}


// map.c: entityCheckEntityCollision for any character but the player: after the step, an edge of
// its square on a side it moves toward lies inside the other's square.
bool leadingEdgeInside(const GroundSquare& mover, glm::vec2 step, const GroundSquare& other) {
  const glm::vec2 centre = mover.centre + step;
  const float half = mover.half;
  const glm::vec2 low = other.centre - glm::vec2{other.half};
  const glm::vec2 high = other.centre + glm::vec2{other.half};
  const bool acrossZ = spansOverlap(centre.y - half, centre.y + half, low.y, high.y);
  const bool acrossX = spansOverlap(centre.x - half, centre.x + half, low.x, high.x);
  const auto inside = [](float edge, float min, float max) { return edge >= min && edge <= max; };
  if (step.x < -AxisEpsilon && acrossZ && inside(centre.x - half, low.x, high.x)) {
    return true;
  }
  if (step.x > AxisEpsilon && acrossZ && inside(centre.x + half, low.x, high.x)) {
    return true;
  }
  if (step.y < -AxisEpsilon && acrossX && inside(centre.y - half, low.y, high.y)) {
    return true;
  }
  return step.y > AxisEpsilon && acrossX && inside(centre.y + half, low.y, high.y);
}

float turnToward(float from, float to, float step) {
  const float delta = std::remainder(to - from, Turn);
  if (std::abs(delta) <= step) {
    return std::remainder(from + delta, Turn);
  }
  return std::remainder(from + std::copysign(step, delta), Turn);
}

// map.c: COLLISION_GRACE_ROTATION tries the four cardinal facings from the nearest to the middle of
// the eighth of a turn the NPC faces to the farthest.
std::array<float, 4> graceFacings(float yaw) {
  const float eighth = Turn / 8.0F;
  const float middle = (std::floor(yaw / eighth) + 0.5F) * eighth;
  std::array<float, 4> facings{0.0F, Turn / 4.0F, Turn / 2.0F, 3.0F * Turn / 4.0F};
  std::ranges::sort(facings, {}, [middle](float facing) {
    return std::abs(std::remainder(facing - middle, Turn));
  });
  return facings;
}

} // namespace

bool squaresOverlap(const GroundSquare& a, const GroundSquare& b, float slack) {
  const float reach = a.half + b.half - slack;
  return std::abs(a.centre.x - b.centre.x) <= reach && std::abs(a.centre.y - b.centre.y) <= reach;
}

FieldNpcs::FieldNpcs(scene::World& world, NpcRules rules, float frameSeconds)
    : world_{&world}, rules_{std::move(rules)}, frameSeconds_{frameSeconds} {}

void FieldNpcs::enterMap(std::span<const assets::MapNpc> records) {
  core::logDebug("[port:npc] removeMapEntities (overworld.c:1180): {} NPCs off, {} records on the "
                 "new map (loadMapDigimon, overworld.c:1026)",
                 npcs_.size(), records.size());
  while (!npcs_.empty()) {
    remove(npcs_.size() - 1);
  }
  records_.assign(records.begin(), records.end());
  contacts_.clear();
}

const assets::MapNpc* FieldNpcs::record(std::size_t slot) const {
  return slot < records_.size() ? &records_[slot] : nullptr;
}

bool FieldNpcs::spawn(const NpcSpawn& spawn) {
  const assets::MapNpc* record = this->record(spawn.slot);
  if (record == nullptr) {
    core::logError("[port:npc] scriptSetDigimon (overworld.c:1093): slot {} past the map's {} NPC "
                   "records",
                   spawn.slot, records_.size());
    return false;
  }
  if (record->typeId != spawn.species) {
    core::logDebug("[port:npc] scriptSetDigimon (overworld.c:1095): slot {} holds species {}, not "
                   "{}; nothing spawned",
                   spawn.slot, record->typeId, spawn.species);
    return false;
  }
  Npc npc{.slot = spawn.slot,
          .scriptId = record->scriptId,
          .half = spawn.radius,
          .autotalk = spawn.autotalk,
          .solid = record->currentHp != 0};
  const auto old = std::ranges::find(npcs_, spawn.slot, &Npc::slot);
  if (old != npcs_.end()) {
    // The path's progress belongs to the slot (MAP_DIGIMON_TABLE), not to the entity.
    npc.activeSection = old->activeSection;
    npc.hasTarget = old->hasTarget;
    npc.waitSeconds = old->waitSeconds;
    npc.target = old->target;
    npc.motion = old->motion;
    remove(static_cast<std::size_t>(old - npcs_.begin()));
  }
  npc.entity = world_->create();
  entt::registry& registry = world_->registry();
  auto& transform = registry.emplace<Transform>(npc.entity);
  scene::place(transform, assets::toWorldPosition(record->position),
               assets::yawFromOriginal(record->rotation.y));
  registry.emplace<scene::ModelRef>(npc.entity, spawn.model);
  registry.emplace<AnimationIntent>(npc.entity, AnimationIntent{.motion = npc.motion});
  registry.emplace<NpcTag>(npc.entity, record->scriptId);
  core::logDebug("[port:npc] scriptSetDigimon (overworld.c:1093): slot {} species {} script id {} "
                 "at ({}, {}) autotalk {} model {} radius {}",
                 spawn.slot, spawn.species, record->scriptId, record->position.x,
                 record->position.z, spawn.autotalk, spawn.model, spawn.radius);
  const auto place = std::ranges::upper_bound(npcs_, spawn.slot, {}, &Npc::slot);
  npcs_.insert(place, npc);
  arrivals_ = true;
  return true;
}

bool FieldNpcs::unload(std::int32_t scriptId) {
  const auto found = std::ranges::find(npcs_, scriptId, &Npc::scriptId);
  if (found == npcs_.end()) {
    core::logDebug("[port:npc] scriptUnloadEntity (overworld.c:1142): no NPC with script id {}",
                   scriptId);
    return false;
  }
  core::logDebug("[port:npc] scriptUnloadEntity (overworld.c:1142): script id {} (slot {}) off",
                 scriptId, found->slot);
  remove(static_cast<std::size_t>(found - npcs_.begin()));
  return true;
}

bool FieldNpcs::setAutotalk(std::int32_t scriptId, bool autotalk) {
  Npc* npc = find(scriptId);
  if (npc == nullptr) {
    core::logDebug("[port:npc] set_autotalk (script_interp.c:1998): no NPC with script id {}",
                   scriptId);
    return false;
  }
  core::logDebug("[port:npc] set_autotalk (script_interp.c:1998): script id {} autotalk {} -> {}",
                 scriptId, npc->autotalk, autotalk);
  npc->autotalk = autotalk;
  return true;
}

std::optional<scene::Entity> FieldNpcs::entityOf(std::int32_t scriptId) const {
  const Npc* npc = find(scriptId);
  return npc != nullptr ? std::optional{npc->entity} : std::nullopt;
}

std::optional<glm::vec3> FieldNpcs::positionOf(std::int32_t scriptId) const {
  const Npc* npc = find(scriptId);
  return npc != nullptr ? std::optional{world_->registry().get<Transform>(npc->entity).position}
                        : std::nullopt;
}

std::optional<std::size_t> FieldNpcs::instanceOf(std::int32_t scriptId) const {
  const Npc* npc = find(scriptId);
  return npc != nullptr ? std::optional{FirstNpcInstance + npc->slot} : std::nullopt;
}

bool FieldNpcs::hold(std::int32_t scriptId, bool held) {
  Npc* npc = find(scriptId);
  if (npc == nullptr) {
    core::logDebug("[port:npc] setMovementEnabled (overworld.c:1870): no NPC with script id {}",
                   scriptId);
    return false;
  }
  setHeld(*npc, held);
  return true;
}

void FieldNpcs::holdAll(bool held) {
  core::logDebug("[port:npc] setMovementEnabled (overworld.c:1870): every NPC held {}", held);
  for (Npc& npc : npcs_) {
    setHeld(npc, held);
  }
}

void FieldNpcs::setHeld(Npc& npc, bool held) {
  if (npc.held != held) {
    core::logDebug("[port:npc] setMovementEnabled (overworld.c:1887): script id {} held {}",
                   npc.scriptId, held);
  }
  if (npc.held && !held) {
    arrivals_ = true;
  }
  npc.held = held;
  if (held) {
    restart(npc);
  }
}

void FieldNpcs::restart(Npc& npc) {
  // overworld.c: clearMapAITable: the path and the reaction to the player start over.
  npc.activeSection = 0;
  npc.hasTarget = false;
  npc.trackState = TrackUnseen;
}

bool FieldNpcs::restartPath(std::int32_t scriptId) {
  Npc* npc = find(scriptId);
  if (npc == nullptr) {
    return false;
  }
  restart(*npc);
  return true;
}

std::vector<NpcBody> FieldNpcs::bodies() const {
  std::vector<NpcBody> bodies;
  for (const Npc& npc : npcs_) {
    if (npc.solid) {
      bodies.push_back({.scriptId = npc.scriptId, .entity = npc.entity, .half = npc.half});
    }
  }
  return bodies;
}

std::optional<std::int32_t> FieldNpcs::scriptedAnimationOf(std::int32_t scriptId) const {
  const Npc* npc = find(scriptId);
  return npc != nullptr ? std::optional{npc->scriptedAnimation} : std::nullopt;
}

bool FieldNpcs::setScriptedAnimation(std::int32_t scriptId, std::int32_t animation) {
  Npc* npc = find(scriptId);
  if (npc == nullptr) {
    core::logDebug("[port:npc] setActiveAnim (overworld.c:1783): no NPC with script id {}",
                   scriptId);
    return false;
  }
  core::logDebug("[port:npc] setActiveAnim (overworld.c:1783): script id {} animation {} -> {}",
                 scriptId, npc->scriptedAnimation, animation);
  npc->scriptedAnimation = animation;
  return true;
}

FieldNpcs::Npc* FieldNpcs::find(std::int32_t scriptId) {
  const auto found = std::ranges::find(npcs_, scriptId, &Npc::scriptId);
  return found != npcs_.end() ? &*found : nullptr;
}

const FieldNpcs::Npc* FieldNpcs::find(std::int32_t scriptId) const {
  const auto found = std::ranges::find(npcs_, scriptId, &Npc::scriptId);
  return found != npcs_.end() ? &*found : nullptr;
}

void FieldNpcs::remove(std::size_t index) {
  world_->destroy(npcs_[index].entity);
  npcs_.erase(npcs_.begin() + static_cast<std::ptrdiff_t>(index));
}

GroundSquare FieldNpcs::squareOf(const Npc& npc) const {
  return {.centre = ground(world_->registry().get<Transform>(npc.entity).position),
          .half = npc.half};
}

float FieldNpcs::speedOf(Motion motion) const {
  switch (motion) {
  case Motion::Walk:
    return rules_.walkSpeed;
  case Motion::Run:
    return rules_.runSpeed;
  case Motion::Idle:
    break;
  }
  return 0.0F;
}

bool FieldNpcs::blocks(glm::vec3 from, glm::vec3 to, float half) const {
  const GroundSquare moved{.centre = ground(to), .half = half};
  return std::ranges::any_of(npcs_, [&](const Npc& npc) {
    if (!npc.solid) {
      return false;
    }
    const GroundSquare other = squareOf(npc);
    return squaresOverlap(moved, other) &&
           glm::length(ground(to) - other.centre) < glm::length(ground(from) - other.centre);
  });
}

bool FieldNpcs::overlaps(glm::vec3 at, float half) const {
  const GroundSquare square{.centre = ground(at), .half = half};
  return std::ranges::any_of(
      npcs_, [&](const Npc& npc) { return npc.solid && squaresOverlap(square, squareOf(npc)); });
}

void FieldNpcs::touchFromPlayer(GroundSquare player, glm::vec2 facing, bool standing,
                                bool confirm) {
  glm::vec2 low = player.centre - glm::vec2{player.half};
  glm::vec2 high = player.centre + glm::vec2{player.half};
  if (confirm && standing) {
    // map.c: entityCheckEntityCollision stretches the player's box toward the sides it faces.
    low.x -= facing.x < -AxisEpsilon ? rules_.talkReach : 0.0F;
    high.x += facing.x > AxisEpsilon ? rules_.talkReach : 0.0F;
    low.y -= facing.y < -AxisEpsilon ? rules_.talkReach : 0.0F;
    high.y += facing.y > AxisEpsilon ? rules_.talkReach : 0.0F;
  }
  for (const Npc& npc : npcs_) {
    if (!npc.solid) {
      continue;
    }
    const GroundSquare other = squareOf(npc);
    if (!spansOverlap(low.x, high.x, other.centre.x - other.half, other.centre.x + other.half) ||
        !spansOverlap(low.y, high.y, other.centre.y - other.half, other.centre.y + other.half)) {
      continue;
    }
    // tamer.c: checkMapInteraction looks at the first NPC the player collides with only.
    if (npc.autotalk || confirm) {
      const NpcContactCause cause =
          npc.autotalk ? NpcContactCause::Touch : NpcContactCause::Confirm;
      core::logDebug("[port:npc] checkMapInteraction (tamer.c:667): player {} script id {} (slot "
                     "{})",
                     cause == NpcContactCause::Touch ? "touches" : "talks to", npc.scriptId,
                     npc.slot);
      contacts_.push_back({.scriptId = npc.scriptId, .entity = npc.entity, .cause = cause});
    }
    return;
  }
}

void FieldNpcs::beginTick() {
  entt::registry& registry = world_->registry();
  for (const Npc& npc : npcs_) {
    scene::beginTick(registry.get<Transform>(npc.entity));
  }
}

void FieldNpcs::tick(float seconds, const assets::MapCollision& collision,
                     const NpcSurroundings& surroundings) {
  entt::registry& registry = world_->registry();
  beginTick();
  for (Npc& npc : npcs_) {
    // overworld.c:1237: a script's animation leaves the NPC to the script.
    if (npc.scriptedAnimation != 0) {
      continue;
    }
    // overworld.c:1344: held, it stands (animation 0).
    if (npc.held) {
      npc.motion = Motion::Idle;
      npc.blocker = Blocker::None;
      registry.get<AnimationIntent>(npc.entity) = {.motion = Motion::Idle, .speedScale = 1.0F};
      continue;
    }
    // overworld.c:1239: the path runs while the NPC has not seen the player.
    if (npc.trackState == TrackUnseen) {
      tickPath(npc, seconds);
    }
    if (rules_.tracking.has_value()) {
      const NpcTrackingRules& tracking = *rules_.tracking;
      const assets::MapNpc& record = records_[npc.slot];
      if (const NpcTrackingMode* mode = trackingOf(tracking, record); mode != nullptr) {
        track(npc, record, tracking, *mode, surroundings.player.centre, seconds);
      }
    }
    move(npc, seconds, collision, surroundings);
  }
}

const NpcTrackingMode* FieldNpcs::trackingOf(const NpcTrackingRules& tracking,
                                             const assets::MapNpc& record) {
  const auto mode = std::ranges::find_if(tracking.modes, [&](const NpcTrackingMode& candidate) {
    return std::ranges::find(candidate.followModes, record.followMode) !=
           candidate.followModes.end();
  });
  return mode != tracking.modes.end() ? &*mode : nullptr;
}

bool FieldNpcs::sees(const Npc& npc, const assets::MapNpc& record, const NpcTrackingRules& tracking,
                     NpcSpot spot, glm::vec2 player) const {
  if (spot == NpcSpot::Rect) {
    // NPCEntityIsInTrackingRect (overworld.c:1389): around the NPC's home, not where it stands.
    const glm::vec2 home = ground(assets::toWorldPosition(record.position));
    const auto range = static_cast<float>(record.trackingRange);
    return std::abs(player.x - home.x) < range && std::abs(player.y - home.y) < range;
  }
  // NPCEntityIsInTrackingRadius (overworld.c:1446) in the original's axes (its z is the world's
  // -z): it adds the signed dx and dz, not their squares, so a player on one side is always seen.
  const glm::vec2 at = ground(world_->registry().get<Transform>(npc.entity).position);
  const float dx = at.x - player.x;
  const float dz = player.y - at.y;
  const std::int32_t radius = record.trackingRange * tracking.radiusTenths / Tenths;
  return dx + dz < static_cast<float>(radius * radius);
}

void FieldNpcs::track(Npc& npc, const assets::MapNpc& record, const NpcTrackingRules& tracking,
                      const NpcTrackingMode& mode, glm::vec2 player, float seconds) {
  // overworld.c:1247: a reaction starts when the NPC sees the player and runs until it ends.
  if (npc.trackState == TrackUnseen && !sees(npc, record, tracking, mode.spot, player)) {
    return;
  }
  switch (mode.reaction) {
  case NpcReaction::Look:
    look(npc, record, tracking, player, seconds);
    break;
  case NpcReaction::Chase:
    chase(npc, record, tracking, player, seconds);
    break;
  case NpcReaction::Follow:
    follow(npc, record, tracking, mode.gait, player, seconds);
    break;
  }
}

void FieldNpcs::look(Npc& npc, const assets::MapNpc& record, const NpcTrackingRules& tracking,
                     glm::vec2 player, float seconds) {
  // NPCEntityTickLookingAtTamer (overworld.c:1400): stand, then turn to the player while it stays
  // in the rectangle.
  if (npc.trackState == TrackUnseen) {
    npc.motion = Motion::Idle;
    setTrackState(npc, TrackStep1);
    return;
  }
  const float height = world_->registry().get<Transform>(npc.entity).position.y;
  turnTo(npc, {player.x, height, player.y}, seconds);
  if (!sees(npc, record, tracking, NpcSpot::Rect, player)) {
    setTrackState(npc, TrackUnseen);
    npc.hasTarget = false;
  }
}

void FieldNpcs::chase(Npc& npc, const assets::MapNpc& record, const NpcTrackingRules& tracking,
                      glm::vec2 player, float seconds) {
  // NPCEntityTickTrackingTamer (overworld.c:1424) and NPCEntityTickTrackingTamer3 (:1548).
  switch (npc.trackState) {
  case TrackUnseen: {
    // Where the player was when seen, walking (animation 2).
    const float height = world_->registry().get<Transform>(npc.entity).position.y;
    npc.target = {player.x, height, player.y};
    npc.motion = Motion::Walk;
    setTrackState(npc, TrackStep1);
    break;
  }
  case TrackStep1:
    npc.motion = Motion::Run;
    setTrackState(npc, TrackStep2);
    break;
  case TrackStep2:
    // It stops on the cell where it saw the player, or when it runs into the player.
    turnTo(npc, npc.target, seconds);
    if (reachedTarget(npc) || npc.blocker == Blocker::Player) {
      npc.motion = Motion::Idle;
      npc.waitSeconds = 0.0F;
      setTrackState(npc, TrackStep3);
    }
    break;
  case TrackStep3:
    if (waited(npc, tracking.chaseWaitFrames, seconds)) {
      npc.motion = Motion::Walk;
      npc.target = assets::toWorldPosition(record.position);
      npc.waitSeconds = 0.0F;
      setTrackState(npc, TrackStep4);
    }
    break;
  case TrackStep4:
    turnTo(npc, npc.target, seconds);
    if (reachedTarget(npc)) {
      npc.motion = Motion::Idle;
      npc.waitSeconds = 0.0F;
      npc.hasTarget = false;
      setTrackState(npc, TrackUnseen);
    }
    break;
  default:
    break;
  }
}

void FieldNpcs::follow(Npc& npc, const assets::MapNpc& record, const NpcTrackingRules& tracking,
                       NpcGait gait, glm::vec2 player, float seconds) {
  // NPCEntityTickTrackingTamer2 (overworld.c:1465) and NPCEntityTickTrackingTamer4 (:1626): after
  // the first step it keeps going while the radius test sees the player, whatever saw it first.
  switch (npc.trackState) {
  case TrackUnseen:
    npc.motion = gait == NpcGait::Run ? Motion::Run : Motion::Walk;
    setTrackState(npc, TrackStep1);
    break;
  case TrackStep1:
    if (sees(npc, record, tracking, NpcSpot::Radius, player)) {
      const float height = world_->registry().get<Transform>(npc.entity).position.y;
      turnTo(npc, {player.x, height, player.y}, seconds);
    } else {
      npc.target = assets::toWorldPosition(record.position);
      setTrackState(npc, TrackStep2);
    }
    break;
  case TrackStep2:
    turnTo(npc, npc.target, seconds);
    if (reachedTarget(npc)) {
      npc.motion = Motion::Idle;
      npc.waitSeconds = 0.0F;
      setTrackState(npc, TrackStep3);
    }
    break;
  case TrackStep3:
    if (waited(npc, tracking.followWaitFrames, seconds)) {
      npc.waitSeconds = 0.0F;
      npc.hasTarget = false;
      setTrackState(npc, TrackUnseen);
    }
    break;
  default:
    break;
  }
}

void FieldNpcs::turnTo(Npc& npc, glm::vec3 target, float seconds) {
  // The chases turn only while nothing blocks the NPC (NPC_COLLISION_STATE -1).
  if (npc.blocker != Blocker::None) {
    return;
  }
  auto& transform = world_->registry().get<Transform>(npc.entity);
  transform.yaw = turnToward(transform.yaw, assets::yawOfDirection(target - transform.position),
                             rules_.turnSpeed * seconds);
}

bool FieldNpcs::reachedTarget(const Npc& npc) const {
  const glm::vec3 position = world_->registry().get<Transform>(npc.entity).position;
  return assets::collisionCellAt(position) == assets::collisionCellAt(npc.target);
}

bool FieldNpcs::waited(Npc& npc, std::int32_t frames, float seconds) const {
  npc.waitSeconds += seconds;
  return npc.waitSeconds >= static_cast<float>(frames) * frameSeconds_;
}

void FieldNpcs::setTrackState(Npc& npc, std::uint8_t state) {
  core::logDebug("[port:encounter] NPCEntityTickOverworld (overworld.c:1247): script id {} "
                 "reaction step {} -> {}",
                 npc.scriptId, npc.trackState, state);
  npc.trackState = state;
}

void FieldNpcs::tickPath(Npc& npc, float seconds) {
  const assets::MapNpc& record = records_[npc.slot];
  if (npc.pathBroken || npc.activeSection >= record.aiSections.size()) {
    return;
  }
  switch (record.aiSections[npc.activeSection]) {
  case PathWait:
    waitOnPath(npc, record, seconds);
    break;
  case PathWalk:
    walkOnPath(npc, record, Motion::Walk, seconds);
    break;
  case PathRun:
    walkOnPath(npc, record, Motion::Run, seconds);
    break;
  default:
    break;
  }
  if (npc.activeSection >= record.aiSections.size() ||
      record.aiSections[npc.activeSection] == PathEnd) {
    npc.activeSection = 0;
  }
}

void FieldNpcs::waitOnPath(Npc& npc, const assets::MapNpc& record, float seconds) const {
  // overworld.c: tickWaypointWait: stand, then count the frames in the waypoint's x.
  if (!npc.hasTarget) {
    npc.motion = Motion::Idle;
    npc.waitSeconds = 0.0F;
    npc.hasTarget = true;
    return;
  }
  if (npc.activeSection >= record.waypoints.size()) {
    core::logError("[port:npc] tickWaypointWait (overworld.c:1706): script id {} waits on waypoint "
                   "{} of {}; it stands",
                   npc.scriptId, npc.activeSection, record.waypoints.size());
    npc.pathBroken = true;
    return;
  }
  npc.waitSeconds += seconds;
  const float wait = static_cast<float>(record.waypoints[npc.activeSection].x) * frameSeconds_;
  if (npc.waitSeconds >= wait) {
    core::logDebug("[port:npc] tickWaypointWait (overworld.c:1706): script id {} waited {} "
                   "frames on part {}",
                   npc.scriptId, record.waypoints[npc.activeSection].x, npc.activeSection);
    npc.hasTarget = false;
    npc.waitSeconds = 0.0F;
    ++npc.activeSection;
  }
}

void FieldNpcs::walkOnPath(Npc& npc, const assets::MapNpc& record, Motion motion, float seconds) {
  // overworld.c: NPCEntityTickWaypointWalk.
  if (!npc.hasTarget) {
    if (npc.activeSection >= record.waypoints.size()) {
      core::logError("[port:npc] NPCEntityTickWaypointWalk (overworld.c:1740): script id {} walks "
                     "to waypoint {} of {}; it stands",
                     npc.scriptId, npc.activeSection, record.waypoints.size());
      npc.pathBroken = true;
      npc.motion = Motion::Idle;
      return;
    }
    npc.target = assets::toWorldPosition(record.waypoints[npc.activeSection]);
    npc.motion = motion;
    npc.hasTarget = true;
    return;
  }
  auto& transform = world_->registry().get<Transform>(npc.entity);
  if (npc.blocker == Blocker::None) {
    transform.yaw =
        turnToward(transform.yaw, assets::yawOfDirection(npc.target - transform.position),
                   rules_.turnSpeed * seconds);
  }
  if (assets::collisionCellAt(transform.position) == assets::collisionCellAt(npc.target)) {
    core::logDebug("[port:npc] NPCEntityTickWaypointWalk (overworld.c:1780): script id {} reached "
                   "waypoint {}",
                   npc.scriptId, npc.activeSection);
    npc.hasTarget = false;
    ++npc.activeSection;
  }
}

FieldNpcs::Blocker FieldNpcs::probe(const Npc& npc, glm::vec2 step,
                                    const assets::MapCollision& collision,
                                    const NpcSurroundings& surroundings) const {
  // map.c: entityCheckCollision: the map first, then the player, the partner and the NPCs in their
  // slots' order.
  const glm::vec3 position = world_->registry().get<Transform>(npc.entity).position;
  const GridMove onGrid = moveOnGrid(collision, position, step, npc.half);
  if (onGrid.blockedX || onGrid.blockedZ) {
    return Blocker::Wall;
  }
  const GroundSquare self = squareOf(npc);
  if (leadingEdgeInside(self, step, surroundings.player)) {
    return Blocker::Player;
  }
  if (leadingEdgeInside(self, step, surroundings.partner)) {
    return Blocker::Partner;
  }
  const bool npcInTheWay = std::ranges::any_of(npcs_, [&](const Npc& other) {
    return &other != &npc && other.solid && leadingEdgeInside(self, step, squareOf(other));
  });
  return npcInTheWay ? Blocker::Npc : Blocker::None;
}

void FieldNpcs::move(Npc& npc, float seconds, const assets::MapCollision& collision,
                     const NpcSurroundings& surroundings) {
  entt::registry& registry = world_->registry();
  auto& transform = registry.get<Transform>(npc.entity);
  registry.get<AnimationIntent>(npc.entity) = {.motion = npc.motion, .speedScale = 1.0F};
  if (npc.motion == Motion::Idle) {
    npc.blocker = Blocker::None;
    return;
  }
  const glm::vec2 facing = ground(assets::forwardOfYaw(transform.yaw));
  // The original tests one frame of its animation's motion ahead.
  const float speed = speedOf(npc.motion);
  const Blocker blocker = probe(npc, facing * (speed * frameSeconds_), collision, surroundings);
  if (blocker != npc.blocker) {
    core::logDebug("[port:npc] entityCheckCollision (map.c:3400): script id {} blocked {} -> {}",
                   npc.scriptId, static_cast<int>(npc.blocker), static_cast<int>(blocker));
  }
  npc.blocker = blocker;
  // overworld.c:1328 (US): an NPC with autotalk that walks into the free player runs its section.
  if (blocker == Blocker::Player && surroundings.playerFree && npc.autotalk) {
    core::logDebug("[port:npc] NPCEntityTickOverworld (overworld.c:1328): script id {} touches "
                   "the player",
                   npc.scriptId);
    contacts_.push_back(
        {.scriptId = npc.scriptId, .entity = npc.entity, .cause = NpcContactCause::Touch});
  }
  const glm::vec2 step = facing * (speed * seconds);
  if (blocker == Blocker::None) {
    transform.position += glm::vec3{step.x, 0.0F, step.y};
    return;
  }
  if (blocker == Blocker::Wall) {
    transform.position = moveOnGrid(collision, transform.position, step, npc.half).position;
  }
  turnFromBlock(npc, seconds, collision, surroundings);
}

void FieldNpcs::turnFromBlock(Npc& npc, float seconds, const assets::MapCollision& collision,
                              const NpcSurroundings& surroundings) {
  // map.c: collisionGrace: turn the short way toward the first free cardinal facing, a little a
  // frame; with none free, keep turning.
  auto& transform = world_->registry().get<Transform>(npc.entity);
  const float probeLength = speedOf(npc.motion) * frameSeconds_;
  for (const float facing : graceFacings(transform.yaw)) {
    const glm::vec2 step = ground(assets::forwardOfYaw(facing)) * probeLength;
    if (probe(npc, step, collision, surroundings) == Blocker::None) {
      transform.yaw = turnToward(transform.yaw, facing, rules_.graceTurnSpeed * seconds);
      return;
    }
  }
  transform.yaw = std::remainder(transform.yaw - (rules_.stuckTurnSpeed * seconds), Turn);
}

} // namespace dwe::game::field
